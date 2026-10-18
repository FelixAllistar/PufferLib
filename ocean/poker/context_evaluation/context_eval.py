#!/usr/bin/env python3
"""Explicit single-job no-stakes Slumbot runner for a retained river context.

Importing this module makes no requests. Existing hand logs are never resumed.
A source-bound series plan must authorize
the exact command before the explicit ``run`` subcommand is invoked.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import sys
from threading import Event, Lock
import time


POLICY_ID = 7
U64_MAX = (1 << 64) - 1
ENDPOINT = "https://slumbot.com/slumbot/api/"
PUBLIC_FIELDS = ("client_pos", "hole_cards", "board", "action")


class JobError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise JobError(message)


def integer(value, low, high, name):
    require(type(value) is int and low <= value <= high, "invalid " + name)
    return value


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def unique_object(pairs):
    obj = {}
    for key, value in pairs:
        require(key not in obj, "duplicate JSON key")
        obj[key] = value
    return obj


def decode(raw):
    def reject_constant(_):
        raise JobError("nonfinite JSON constant")
    return json.loads(raw, object_pairs_hook=unique_object, parse_constant=reject_constant)


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    require(spec is not None and spec.loader is not None, "cannot load frozen module")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def quotas(hands, games):
    integer(hands, 2, 10000000, "hand quota")
    integer(games, 1, 50, "worker count")
    require(games <= hands // 2, "every worker needs an even quota of at least two hands")
    pairs, odd = divmod(hands, 2)
    return [2 * (pairs // games + int(game < pairs % games)) + (odd if game == 0 else 0)
            for game in range(games)]


def hand_seed(master, reservation):
    integer(master, 0, U64_MAX, "master seed")
    integer(reservation, 0, U64_MAX, "reservation")
    key = f"poker-policy-hand-seed-v1:{master}:{reservation}".encode("ascii")
    return int.from_bytes(hashlib.sha256(key).digest()[:8], "big") or 1


def action_token(action, state, rows):
    integer(action, -1, 20000, "native action")
    return "f" if action < 0 else f"b{action}" if action else (
        "c" if state[3] > rows[state[0]][3] else "k")


def public_view(response):
    require(type(response) is dict, "response must be a JSON object")
    require(all(key in response for key in PUBLIC_FIELDS), "incomplete public response")
    require(type(response["hole_cards"]) is list and type(response["board"]) is list,
            "card fields must be arrays")
    # Deep copy only the declared decision-visible values. Terminal revelations
    # and server outcome fields cannot enter Session or any policy call.
    return {key: list(response[key]) if key in ("hole_cards", "board") else response[key]
            for key in PUBLIC_FIELDS}


class Journal:
    """One serialized append path; a reservation is on disk before its POST."""
    def __init__(self, path, metadata):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.stream = self.path.open("x", encoding="utf-8")
        self.lock = Lock()
        self.completed = 0
        self.started = time.monotonic()
        self.write({"metadata": metadata}, durable=True)

    def write(self, record, durable=False):
        with self.lock:
            self.stream.write(json.dumps(record, sort_keys=True, allow_nan=False) + "\n")
            self.stream.flush()
            if durable:
                os.fsync(self.stream.fileno())

    def terminal(self, record):
        with self.lock:
            record = {"hand": self.completed, **record}
            self.stream.write(json.dumps(record, sort_keys=True, allow_nan=False) + "\n")
            self.stream.flush()
            os.fsync(self.stream.fileno())
            self.completed += 1
            if self.completed % 250 == 0:
                print(json.dumps({"completed_hands": self.completed,
                                  "elapsed_seconds": round(time.monotonic() - self.started, 3)}),
                      flush=True)

    def close(self, complete, failures, worker_counts):
        self.write({"summary": {"complete": complete, "hands": self.completed,
                                "worker_counts": worker_counts, "failures": len(failures),
                                "elapsed_seconds": time.monotonic() - self.started,
                                "closed_utc": datetime.now(timezone.utc).isoformat()}}, durable=True)
        self.stream.close()


def run(args, *, http_factory=None):
    require(args.style == POLICY_ID and args.samples == 0, "unsupported frozen policy configuration")
    allocation = quotas(args.hands, args.games)
    integer(args.seed, 0, U64_MAX, "master seed")
    require(sha256(args.library) == args.expected_library_sha256, "frozen library differs")
    require(sha256(args.control_library) == args.expected_control_sha256, "settlement library differs")
    require(args.arm in ("control", "candidate"), "invalid policy arm")
    require(sha256(args.context_library) == args.expected_context_sha256, "context/parser library differs")
    require(args.expected_library_sha256 == (args.expected_context_sha256 if args.arm == "candidate"
                                            else args.expected_control_sha256), "policy arm/library differs")
    require(type(args.job) is str and args.job and type(args.run_id) is str and args.run_id,
            "missing run/job namespace")
    evaluation = load_module("context_eval_original", args.poker_source / "eval.py")
    # The transport is a separately frozen, network-free sibling module.
    transport = load_module("context_eval_transport", args.context_source / "continuation_transport.py")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    adapter = load_module("context_eval_adapter", Path(__file__).with_name("context_adapter.py"))
    synthetic_transport = http_factory is not None
    if http_factory is None:
        import requests
        http_factory = requests.Session
    source_hashes = {"context_eval.py": sha256(Path(__file__)),
                     "eval.py": sha256(args.poker_source / "eval.py"),
                     "context_adapter.py": sha256(Path(__file__).with_name("context_adapter.py")),
                     "context_audit.py": sha256(Path(__file__).with_name("context_audit.py")),
                     "continuation_transport.py": sha256(args.context_source / "continuation_transport.py")}
    metadata = {"log_schema": 5, "opponent": "synthetic_local_opponent" if synthetic_transport else "slumbot",
                "transport_mode": "synthetic_injected" if synthetic_transport else "live",
                "no_stakes": True,
                "created_utc": datetime.now(timezone.utc).isoformat(),
                "run_id": args.run_id, "job_id": args.job, "arm": args.arm,
                "style": args.style, "equity_samples": args.samples,
                "library_sha256": args.expected_library_sha256,
                "settlement_library_sha256": args.expected_control_sha256,
                "context_library_sha256": args.expected_context_sha256,
                "source_sha256": source_hashes, "manifest_sha256": args.manifest_sha256,
                "seed": args.seed, "hands_requested": args.hands,
                "starting_bb": 200, "concurrent_games": args.games,
                "policy_rng_mode": "independent_hand_seeds_v1",
                "hand_seed_derivation": "sha256_poker_policy_hand_seed_v1",
                "hand_reservation_scheme": "game_blocks_without_overflow_v2",
                "hand_reservation_quotas": allocation,
                "context_protocol": "retained_river_context_recovery_v1",
                "wager_retry_count": 0, "replacement_hands_allowed": False,
                "resume_allowed": False}
    journal = Journal(args.output, metadata)
    stop = Event()
    worker_counts = [0] * args.games
    failures = []
    failures_lock = Lock()

    def play(game, quota):
        http = http_factory()
        http.headers["User-Agent"] = "PufferLib-poker-context-research/1 (" + args.contact + ")"
        contexts, natives = [], []
        token = None
        sessions = []

        def abandon_all():
            for session in sessions:
                try:
                    session.abandon()
                except Exception:
                    # Preserve the original failure; the worker never reuses a
                    # token or context after this path. Object cleanup follows.
                    pass

        def post(endpoint, payload):
            # Exactly one request. A timeout or rejected/ambiguous response
            # never causes us to send an action a second time.
            response = http.post(ENDPOINT + endpoint, json=payload, timeout=(10, 45))
            require(response.status_code == 200, "HTTP status " + str(response.status_code))
            value = decode(response.content)
            require(type(value) is dict and "error_msg" not in value and "error" not in value,
                    "server returned an error or non-object")
            return value

        try:
            for seat in (0, 1):
                native = evaluation.Native(players=2, library=args.library, samples=args.samples)
                owner = f"{args.run_id}/{args.job}/worker{game}/lane0/seat{seat}/actual"
                if args.arm == "candidate":
                    context = transport.Context(native, owner, seat, policy=args.style, samples=args.samples)
                else:
                    parser = evaluation.Native(players=2, library=args.context_library)
                    context = adapter.LegacySession(native, parser, evaluation, transport, owner, seat,
                                                    policy=args.style, samples=args.samples)
                contexts.append(context)
                natives.append(native)
            settlement = evaluation.Native(players=2, library=args.control_library)
            for attempt in range(quota):
                if stop.is_set():
                    break
                index = sum(allocation[:game]) + attempt
                generation = attempt + 1
                seed = hand_seed(args.seed, index)
                identity = {"game": game, "game_hand_attempt": attempt,
                            "hand_reservation_index": index, "policy_seed_at_hand_start": seed,
                            "context_generation": generation}
                decisions = []
                stage = "reserve"
                sessions = []
                try:
                    for seat in (0, 1):
                        natives[seat].rng.value = seed
                        contexts[seat].begin(generation)
                        sessions.append(transport.Session(contexts[seat]) if args.arm == "candidate" else contexts[seat])
                    journal.write({"hand_reserved": identity}, durable=True)
                    stage = "new_hand"
                    request_generation = generation
                    data = post("new_hand", {"token": token} if token is not None else {})
                    seat = integer(data.get("client_pos"), 0, 1, "client position")
                    sessions[1 - seat].abandon()
                    session, native, context = sessions[seat], natives[seat], contexts[seat]
                    request_id = 0
                    while True:
                        require(integer(data.get("client_pos"), 0, 1, "client position") == seat,
                                "response changed client seat")
                        next_token = data.get("token", token)
                        require(type(next_token) is str and 0 < len(next_token) <= 512,
                                "missing or invalid session token")
                        token = next_token
                        visible = public_view(data)
                        terminal = "winnings" in data
                        # The terminal marker allows closing the policy context;
                        # the payoff and revealed opponent cards stay outside it.
                        visible["terminal"] = terminal
                        ingested = session.ingest(request_generation, visible)
                        require(type(ingested) is dict and type(ingested.get("terminal")) is bool
                                and ingested["terminal"] == terminal, "transport/terminal mismatch")
                        if terminal:
                            stage = "terminal_verification"
                            terminal_audit = context.audit()
                            winnings = integer(data["winnings"], -20000, 20000, "terminal winnings")
                            adjusted = data.get("baseline_winnings")
                            require(type(adjusted) in (int, float) and not isinstance(adjusted, bool)
                                    and math.isfinite(adjusted), "missing finite adjusted return")
                            other = data.get("slumbot_hole_cards", data.get("bot_hole_cards"))
                            require(type(other) is list and len(other) == 2, "missing terminal opponent cards")
                            if "slumbot_hole_cards" in data and "bot_hole_cards" in data:
                                require(sorted(data["slumbot_hole_cards"]) == sorted(data["bot_hole_cards"]),
                                        "conflicting terminal opponent cards")
                            settled = evaluation.verify_terminal(settlement, {
                                **visible, "slumbot_hole_cards": list(other), "winnings": winnings})
                            require(settled is True, "terminal payout verification failed")
                            journal.terminal({**identity, "position": seat, "hole_cards": visible["hole_cards"],
                                "board": visible["board"], "action": visible["action"],
                                "slumbot_hole_cards": list(other), "winnings_chips": winnings,
                                "baseline_winnings_chips": adjusted, "policy_rng": native.rng.value,
                                "native_terminal_verified": True, "context_terminal_audit": terminal_audit,
                                "decisions": decisions})
                            worker_counts[game] += 1
                            break
                        before_snapshot = native.snapshot()
                        state, rows, _ = before_snapshot
                        require(state[0] == seat, "native actor/response mismatch")
                        before_audit = context.audit()
                        before_rng = native.rng.value
                        request_id += 1
                        action = session.decide(request_id)
                        require(native.snapshot() == before_snapshot,
                                "policy changed engine state")
                        move = action_token(action, state, rows)
                        hero = rows[seat]
                        decisions.append({**visible, "context_request_id": request_id,
                            "context_generation": generation,
                            "context_before": before_audit, "context_after": context.audit(),
                            "street": state[1], "pot_chips": state[2],
                            "call_chips": min(state[3] - hero[3], hero[0]), "stack_chips": hero[0],
                            "street_commitment_chips": hero[3], "high_bet_chips": state[3],
                            "full_raise_increment_chips": state[4], "legal_abstract_amounts": native.actions(),
                            "sent": move, "native_action": action, "policy_rng_before": before_rng,
                            "policy_rng_after": native.rng.value})
                        stage = "act"
                        request_generation = generation
                        data = post("act", {"token": token, "incr": move})
                except Exception as exc:
                    abandon_all()
                    token = None
                    # Error strings from HTTP libraries can contain request
                    # details. Keep only class/stage and whitelisted local codes.
                    event = {**identity, "stage": stage, "error_type": type(exc).__name__,
                             "reason": str(exc) if type(exc) is JobError else "operation_failed",
                             "decisions": decisions, "replacement_started": False}
                    journal.write({"interrupted": event}, durable=True)
                    with failures_lock:
                        failures.append(event)
                    stop.set()
                    break
        except Exception as exc:
            event = {"game": game, "stage": "worker_setup_or_cleanup", "error_type": type(exc).__name__}
            journal.write({"worker_error": event}, durable=True)
            with failures_lock:
                failures.append(event)
            stop.set()
        finally:
            abandon_all()
            for context in contexts:
                context.close()
            http.close()

    try:
        with ThreadPoolExecutor(max_workers=args.games) as executor:
            jobs = [executor.submit(play, game, quota) for game, quota in enumerate(allocation)]
            for job in jobs:
                try:
                    job.result()
                except Exception as exc:
                    stop.set()
                    with failures_lock:
                        failures.append({"stage": "worker_setup_or_cleanup", "error_type": type(exc).__name__})
        complete = not failures and worker_counts == allocation and journal.completed == args.hands
        journal.close(complete, failures, worker_counts)
    except BaseException:
        stop.set()
        if not journal.stream.closed:
            journal.close(False, failures + [{"stage": "process_interrupted"}], worker_counts)
        raise
    print(json.dumps({"complete": complete, "hands": journal.completed,
                      "output_sha256": sha256(args.output)}), flush=True)
    return 0 if complete else 2


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("run",))
    parser.add_argument("--poker-source", type=Path, required=True)
    parser.add_argument("--context-source", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--expected-library-sha256", required=True)
    parser.add_argument("--control-library", type=Path, required=True)
    parser.add_argument("--expected-control-sha256", required=True)
    parser.add_argument("--context-library", type=Path, required=True)
    parser.add_argument("--expected-context-sha256", required=True)
    parser.add_argument("--arm", choices=("control", "candidate"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--hands", type=int, required=True)
    parser.add_argument("--games", type=int, default=5)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--style", type=int, default=7)
    parser.add_argument("--samples", type=int, default=0)
    parser.add_argument("--job", required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--contact", default="https://github.com/FelixAllistar/PufferLib")
    return run(parser.parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
