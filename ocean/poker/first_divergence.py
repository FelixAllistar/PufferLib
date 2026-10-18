#!/usr/bin/env python3
"""Offline first-divergence comparison of two frozen heads-up range policies.

Every planned hand is required. Policies see only decision-time public records;
a separate engine receives revealed cards for terminal checks and the one fixed
baseline. No HTTP calls are made. Historical mode is explicitly diagnostic.

The supported policy state is the public engine state plus the external policy
RNG (style 7, heads-up). Additional mutable policy memory is not supported.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics
import sys
from typing import Any

ARMS = ("control", "candidate")
VERSION = "first_divergence_v1"
BASELINE_RULE = "bb_minraise_fold_else_river_all_check_call_v1"
RNG_MODE = "independent_hand_seeds_v1"
SEED_DERIVATION = "sha256_poker_policy_hand_seed_v1"
RESERVATION_SCHEME = "game_blocks_with_overflow_v1"
SUPPORT_SOURCES = ("eval.py", "audit_archived_series.py", "slumbot_series.py", "poker_audit.py")
CHIPS_PER_BB = 100
TOKEN = re.compile(r"b\d+|[kcf/]")
CARD = re.compile(r"[23456789TJQKA][cdhs]")
PRIVATE_KEYS = {"slumbot_hole_cards", "bot_hole_cards", "opponent_hole_cards"}
PUBLIC_KEYS = ("client_pos", "hole_cards", "board", "action")


class AnalysisError(ValueError):
    """Incomplete, ambiguous, incompatible, or unreproducible evidence."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AnalysisError(message)


def integer(value: Any, label: str, low: int = 0, high: int | None = None) -> int:
    require(type(value) is int and value >= low and (high is None or value <= high),
            f"Invalid {label}: expected integer in [{low}, {high}]")
    return value


def fingerprint(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def unique_object(pairs: list[tuple[str, Any]]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_log(path: Path, expected_sha256: str) -> list[dict]:
    raw = path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == expected_sha256,
            f"Log changed or differs from completed-job provenance: {path}")
    records = [json.loads(line, object_pairs_hook=unique_object)
               for line in raw.splitlines() if line.strip()]
    require(records and all(isinstance(record, dict) for record in records),
            f"Invalid JSONL records: {path}")
    require(sum("metadata" in row for row in records) == 1 and "metadata" in records[0],
            f"Expected exactly one initial metadata record: {path}")
    require(sum("summary" in row for row in records) == 1 and "summary" in records[-1],
            f"Expected exactly one final summary record: {path}")
    require(not any("interrupted" in row or "resumed" in row for row in records),
            f"Interrupted or resumed log cannot supply full coverage: {path}")
    return records


def estimate(values: list[float]) -> dict:
    n = len(values)
    require(n >= 2 and all(math.isfinite(v) for v in values),
            "An estimate needs at least two finite whole-hand observations")
    mean, variance = statistics.fmean(values), statistics.variance(values)
    rate, se = 100 * mean, 100 * math.sqrt(variance / n)
    half = 1.96 * se
    return {"hands": n, "net_bb": math.fsum(values), "bb100": rate,
            "variance_bb2": variance, "se_bb100": se,
            "ci95_half_width_bb100": half, "ci95_bb100": [rate - half, rate + half]}


def difference(candidate: dict, control: dict) -> dict:
    gain = candidate["bb100"] - control["bb100"]
    se = math.hypot(candidate["se_bb100"], control["se_bb100"])
    half = 1.96 * se
    return {"gain_bb100": gain, "se_bb100": se,
            "ci95_half_width_bb100": half, "ci95_bb100": [gain - half, gain + half]}


def hand_seed(master: int, reservation: int) -> int:
    integer(master, "master seed")
    integer(reservation, "hand reservation")
    message = f"poker-policy-hand-seed-v1:{master}:{reservation}".encode("ascii")
    return int.from_bytes(hashlib.sha256(message).digest()[:8], "big") or 1


def game_quotas(hands: int, games: int) -> list[int]:
    pairs, odd = divmod(hands, 2)
    return [2 * (pairs // games + (game < pairs % games)) + (odd if game == 0 else 0)
            for game in range(games)]


def arm_identities(manifest: dict) -> dict[str, dict]:
    result = {}
    for arm in ARMS:
        jobs = [job for job in manifest["jobs"] if job["arm"] == arm]
        require(jobs, f"Missing arm: {arm}")
        identities = {(job["library_sha256"], job["policy_id"], job["equity_samples"])
                      for job in jobs}
        require(len(identities) == 1, f"{arm} changes library, policy, or sample setting")
        digest, style, samples = next(iter(identities))
        require(style == 7 and samples in (0, 32, 64, 128),
                "This analyzer supports stateless heads-up range policy 7 only")
        require(isinstance(digest, str) and re.fullmatch(r"[0-9a-f]{64}", digest),
                f"Invalid library SHA256 for {arm}")
        result[arm] = {"library_sha256": digest, "policy_id": style,
                       "equity_samples": samples, "recorded_library": jobs[0]["library"]}
    return result


def confirmatory_spec(manifest: dict, analyzer_sha256: str, sources: dict) -> list[list[str]]:
    require(manifest.get("schema") == 2 and
            manifest.get("primary_metric") == "first_divergence_gain_bb100" and
            manifest.get("secondary_metrics") == ["raw_gain_bb100", "already_adjusted_gain_bb100"],
            "Confirmatory mode requires the frozen schema-2 first-divergence metric specification")
    spec = manifest.get("first_divergence", {})
    expected = {"version": VERSION, "baseline_rule": BASELINE_RULE,
                "policy_rng_mode": RNG_MODE, "hand_seed_derivation": SEED_DERIVATION,
                "hand_reservation_scheme": RESERVATION_SCHEME,
                "analyzer_sha256": analyzer_sha256, "support_sources_sha256": sources}
    for key, value in expected.items():
        require(spec.get(key) == value, f"Frozen first-divergence specification mismatch: {key}")
    require(sources["eval.py"] == manifest["runner_sha256"],
            "Confirmatory replay runner differs from the frozen live runner")
    by_id = {job["id"]: job for job in manifest["jobs"]}
    require(len(by_id) == len(manifest["jobs"]), "Duplicate planned job IDs")
    pairs = spec.get("paired_jobs")
    require(isinstance(pairs, list) and len(pairs) == len(by_id) // 2,
            "Expected explicit paired_jobs covering the whole series")
    used, masters = [], []
    for pair in pairs:
        require(isinstance(pair, list) and len(pair) == 2 and all(i in by_id for i in pair),
                "Invalid first-divergence job pair")
        a, b = (by_id[i] for i in pair)
        require({a["arm"], b["arm"]} == set(ARMS), "Each pair needs both arms")
        require(all(a[key] == b[key] for key in ("seed", "quota", "games")),
                "Paired jobs must have identical master seeds, quotas, and game counts")
        masters.append(a["seed"])
        used.extend(pair)
    require(len(set(used)) == len(used) and set(used) == set(by_id),
            "Paired jobs must cover each planned job exactly once")
    require(len(set(masters)) == len(masters), "Pairs need distinct independent master seeds")
    return pairs


def seeded_log_signature(job: dict, records: list[dict]) -> dict[int, tuple]:
    metadata = records[0]["metadata"]
    for key, expected in {"log_schema": 4, "policy_rng_mode": RNG_MODE,
                          "hand_seed_derivation": SEED_DERIVATION,
                          "hand_reservation_scheme": RESERVATION_SCHEME,
                          "seed": job["seed"]}.items():
        require(metadata.get(key) == expected, f"{job['id']}: independent-hand metadata mismatch: {key}")
    n, games = integer(job["quota"], "quota", 2), integer(job["games"], "games", 1, 50)
    quotas = game_quotas(n, games)
    require(metadata.get("hand_reservation_quotas") == quotas,
            f"{job['id']}: hand reservation quotas differ from the fixed allocation")
    reserved, finished = {}, {}

    def identity(record: dict) -> tuple[int, tuple]:
        game = integer(record.get("game"), "reservation game", 0, games - 1)
        attempt = integer(record.get("game_hand_attempt"), "game hand attempt")
        require(attempt < quotas[game], f"{job['id']}: spill/overflow reservation is ineligible")
        index = integer(record.get("hand_reservation_index"), "reservation index", 0, n - 1)
        require(index == sum(quotas[:game]) + attempt, f"{job['id']}: wrong reservation index")
        seed = integer(record.get("policy_seed_at_hand_start"), "hand seed", 1, 2**64 - 1)
        require(seed == hand_seed(job["seed"], index), f"{job['id']}: wrong derived hand seed")
        return index, (game, attempt, seed)

    for row in records:
        if "hand_reserved" in row:
            index, data = identity(row["hand_reserved"])
            require(index not in reserved, f"{job['id']}: duplicated hand reservation")
            reserved[index] = data
        if "winnings_chips" not in row:
            continue
        index, data = identity(row)
        require(index in reserved and reserved[index] == data,
                f"{job['id']}: terminal record lacks a matching earlier reservation")
        require(index not in finished, f"{job['id']}: duplicated terminal reservation")
        position = integer(row.get("position"), "position", 0, 1)
        decisions = row.get("decisions")
        require(isinstance(decisions, list), f"{job['id']}: missing decision trace")
        if decisions:
            require(decisions[0].get("policy_rng_before") == data[2],
                    f"{job['id']}: first policy RNG differs from the independent hand seed")
        else:
            require(row.get("policy_rng") == data[2],
                    f"{job['id']}: no-decision hand changed its policy RNG")
        finished[index] = (*data, position)
    require(set(reserved) == set(finished) == set(range(n)),
            f"{job['id']}: incomplete planned reservation coverage")
    return finished


def cards(value: Any, label: str, lengths: tuple[int, ...]) -> list[str]:
    require(isinstance(value, list) and len(value) in lengths and
            all(isinstance(c, str) and CARD.fullmatch(c) for c in value) and len(set(value)) == len(value),
            f"Invalid {label}")
    return value


def history_tokens(value: Any) -> list[str]:
    require(isinstance(value, str), "Action history must be text")
    tokens = TOKEN.findall(value)
    require("".join(tokens) == value, "Malformed action history")
    return tokens


def public_decision(decision: dict) -> dict:
    require(isinstance(decision, dict), "Decision must be an object")
    require(not PRIVATE_KEYS.intersection(decision), "Private opponent cards in decision-time record")
    integer(decision.get("client_pos"), "decision position", 0, 1)
    own = cards(decision.get("hole_cards"), "hero cards", (2,))
    board = cards(decision.get("board"), "decision board", (0, 3, 4, 5))
    require(len(set(own + board)) == len(own + board), "Colliding public cards")
    history_tokens(decision.get("action"))
    integer(decision.get("policy_rng_before"), "policy RNG before", 1, 2**64 - 1)
    integer(decision.get("policy_rng_after"), "policy RNG after", 1, 2**64 - 1)
    integer(decision.get("native_action"), "native action", -1, 20000)
    integer(decision.get("street"), "street", 0, 3)
    # An explicit allowlist is the only payload passed to policy replay.
    return {key: decision[key] for key in PUBLIC_KEYS}


def revealed_cards(hand: dict) -> list[str]:
    available = [hand[key] for key in ("slumbot_hole_cards", "bot_hole_cards") if key in hand]
    require(available, "Missing required terminal opponent-card revelation")
    for value in available:
        cards(value, "revealed opponent cards", (2,))
    require(all(set(value) == set(available[0]) for value in available),
            "Ambiguous opponent-card revelation aliases")
    return available[0]


def check_snapshot(decision: dict, state: list, players: list, legal: list) -> None:
    hero = decision["client_pos"]
    require(state[0] == hero, "Decision record is not the engine's acting player")
    row = players[hero]
    expected = {"street": state[1], "pot_chips": state[2],
                "call_chips": min(state[3] - row[3], row[0]), "stack_chips": row[0],
                "street_commitment_chips": row[3], "high_bet_chips": state[3],
                "full_raise_increment_chips": state[4], "legal_abstract_amounts": legal}
    for key, value in expected.items():
        require(decision.get(key) == value, f"Decision/native public-state mismatch: {key}")


def action_token(action: int, state: list, players: list) -> str:
    return "f" if action < 0 else f"b{action}" if action else (
        "c" if state[3] > players[state[0]][3] else "k")


def verify_terminal_trace(evaluation: Any, settlement: Any, hand: dict) -> int:
    """Check every actual action prefix and payoff without calling any policy.

    Revealed cards may enter this engine. This object is never a policy engine.
    The board in policy replays remains the separate decision-time board.
    """
    hero = integer(hand.get("position"), "terminal position", 0, 1)
    own = cards(hand.get("hole_cards"), "terminal hero cards", (2,))
    board = cards(hand.get("board"), "terminal board", (0, 3, 4, 5))
    other = revealed_cards(hand)
    require(len(set(own + board + other)) == len(own + board + other), "Colliding terminal cards")
    integer(hand.get("winnings_chips"), "terminal winnings", -20000, 20000)
    require(hand.get("native_terminal_verified") is True, "Live runner did not verify the terminal payoff")
    decisions = hand.get("decisions")
    require(isinstance(decisions, list), "Missing decision trace")
    tokens = history_tokens(hand.get("action"))
    evaluation.replay(settlement, {"client_pos": hero, "hole_cards": own, "board": board,
                                  "action": "", "slumbot_hole_cards": other})
    prefix, index, prior_rng = "", 0, None
    for token in tokens:
        if token == "/":
            prefix += token
            continue
        state, players, _ = settlement.snapshot()
        require(state[0] != 255, "Action appears after the native hand ended")
        if state[0] == hero:
            require(index < len(decisions), "Missing hero decision in terminal action trace")
            decision = decisions[index]
            public_decision(decision)
            require(decision["client_pos"] == hero and decision["action"] == prefix,
                    "Decision prefix/position does not match the complete terminal trace")
            require(set(decision["hole_cards"]) == set(own) and
                    decision["board"] == board[:(0, 3, 4, 5)[state[1]]],
                    "Decision-time cards do not match the terminal cards and street")
            check_snapshot(decision, state, players, settlement.actions())
            require(action_token(decision["native_action"], state, players) == token == decision.get("sent"),
                    "Recorded native/sent action differs from the terminal trace")
            require(prior_rng is None or decision["policy_rng_before"] == prior_rng,
                    "Broken within-hand recorded RNG chain")
            prior_rng = decision["policy_rng_after"]
            index += 1
        settlement.step(int(token[1:]) if token.startswith("b") else -1 if token == "f" else 0)
        prefix += token
    state, players, _ = settlement.snapshot()
    require(state[0] == 255, "Terminal history does not finish the native hand")
    require(index == len(decisions), "Extra hero decision outside the terminal trace")
    require(players[hero][0] - players[hero][1] == hand["winnings_chips"],
            "Terminal return differs from independently replayed native settlement")
    if decisions:
        require(hand.get("policy_rng") == prior_rng, "Terminal RNG differs from the final recorded decision")
    return index


def first_baseline(evaluation: Any, settlement: Any, hand: dict, decision: dict,
                   actions: dict, state: list, players: list) -> dict:
    hero = decision["client_pos"]
    if decision["street"] == 0:
        require(hero != state[8] and state[9] == 1 and state[3] == 200 and
                players[0][1] == players[1][1] == 20000 and decision["action"] == "b200" and
                actions == {"control": -1, "candidate": 0},
                "Unsupported first preflop divergence; only the frozen BB minraise fold/call gate is allowed")
        baseline_chips = players[hero][0] - players[hero][1]
        require(baseline_chips == -100, "BB fold baseline is not exactly -1 BB")
        return {"control_variate_kind": "bb_minraise_fold", "control_variate_chips": baseline_chips}
    require(decision["street"] == 3 and len(decision["board"]) == 5,
            "Unsupported first divergence outside the BB minraise gate or completed river")
    other = revealed_cards(hand)
    baseline_state, baseline_players, _ = evaluation.replay(
        settlement, {**public_decision(decision), "slumbot_hole_cards": other})
    require(baseline_state == state and
            all(a[:5] == b[:5] for a, b in zip(baseline_players, players)),
            "Revealed-card settlement changed the pre-divergence public money state")
    initial = baseline_players[hero][1]
    steps, closes = 0, False
    while baseline_state[0] != 255:
        require(baseline_state[1] == 3 and steps < 2, "Passive river continuation did not close within two actions")
        settlement.step(0)
        steps += 1
        baseline_state, baseline_players, _ = settlement.snapshot()
        if steps == 1:
            closes = baseline_state[0] == 255
    return {"control_variate_kind": "river_all_check_call",
            "control_variate_chips": baseline_players[hero][0] - initial,
            "settlement_opponent_cards": other, "baseline_passive_steps": steps,
            "actual_passive_would_close": closes}


def analyze_hand(evaluation: Any, policies: dict, settlement: Any,
                 identities: dict, job: dict, hand: dict) -> dict:
    require(all(settlement is not native for native in policies.values()),
            "Settlement and policy engines must be separate objects")
    terminal_checks = verify_terminal_trace(evaluation, settlement, hand)
    arm, decisions = job["arm"], hand["decisions"]
    row = {"job": job["id"], "arm": arm, "hand": hand["hand"], "game": hand["game"],
           "position": hand["position"], "actual_net_bb": hand["winnings_chips"] / CHIPS_PER_BB,
           "already_adjusted_net_bb": hand["baseline_winnings_chips"] / CHIPS_PER_BB,
           "diverged": False, "control_variate_kind": "no_divergence_common_return",
           "control_variate_bb": hand["winnings_chips"] / CHIPS_PER_BB, "residual_bb": 0.0,
           "terminal_action_history": hand["action"], "prefix_decision_pairs_checked": 0,
           "terminal_trace_decisions_checked": terminal_checks, "terminal_return_verified": True}
    for key in ("game_hand_attempt", "hand_reservation_index", "policy_seed_at_hand_start"):
        if key in hand:
            row[key] = hand[key]
    shadows = {a: decisions[0]["policy_rng_before"] for a in ARMS} if decisions else {}
    if decisions:
        row["hand_start_policy_rng"] = decisions[0]["policy_rng_before"]
    for index, decision in enumerate(decisions):
        actions, after, snapshots = {}, {}, {}
        visible = public_decision(decision)
        for policy in ARMS:
            native = policies[policy]
            require(shadows[policy] == decision["policy_rng_before"], "Shadow RNG differs before first divergence")
            before = evaluation.replay(native, visible)
            check_snapshot(decision, before[0], before[1], native.actions())
            native.rng.value = shadows[policy]
            actions[policy] = native.bot(identities[policy]["policy_id"])
            after[policy] = native.rng.value
            snapshots[policy] = native.snapshot()
            require(snapshots[policy] == before, "Policy mutated the native engine state")
        require(snapshots["control"] == snapshots["candidate"], "Policy replays have different common-prefix engine states")
        row["prefix_decision_pairs_checked"] += 1
        require(actions[arm] == decision["native_action"], "Actual-policy action replay mismatch")
        require(after[arm] == decision["policy_rng_after"], "Actual-policy RNG replay mismatch")
        if actions["control"] == actions["candidate"] and after["control"] == after["candidate"]:
            shadows = after
            continue
        row.update(diverged=True, first_decision_index=index, first_state=dict(decision),
                   first_actions=actions, first_rng_after=after,
                   action_divergence=actions["control"] != actions["candidate"],
                   rng_divergence=after["control"] != after["candidate"])
        state, players, _ = snapshots["control"]
        baseline = first_baseline(evaluation, settlement, hand, decision, actions, state, players)
        chips = baseline.pop("control_variate_chips")
        row.update(baseline, control_variate_bb=chips / CHIPS_PER_BB,
                   residual_bb=(hand["winnings_chips"] - chips) / CHIPS_PER_BB)
        actual_closes = (row["control_variate_kind"] == "bb_minraise_fold" and actions[arm] < 0) or (
            row["control_variate_kind"] == "river_all_check_call" and actions[arm] == 0 and
            row["actual_passive_would_close"])
        if actual_closes:
            require(hand["winnings_chips"] == chips, "Actual closing action differs from the identical baseline settlement")
            row["actual_terminal_baseline_verified"] = True
        break  # Exactly one baseline; later river changes in a BB-rescue hand are already included.
    return row


def summarize(rows: list[dict], expected_hands: int) -> tuple[dict, dict, dict]:
    require(len(rows) == 2 * expected_hands and all(row["arm"] in ARMS for row in rows),
            "Incomplete or extra rows in the all-hand denominator")
    counts = Counter({key: 0 for key in (
        "hands", "prefix_decision_pairs_checked", "terminal_trace_decisions_checked",
        "terminal_returns_verified", "no_hero_decision_hands", "no_divergence_hands",
        "bb_minraise_fold", "river_all_check_call", "first_divergence_action_only",
        "first_divergence_rng_only", "first_divergence_action_and_rng",
        "bb_actual_fold_equals_baseline", "river_actual_passive_equals_baseline")})
    for row in rows:
        counts["hands"] += 1
        counts["prefix_decision_pairs_checked"] += row["prefix_decision_pairs_checked"]
        counts["terminal_trace_decisions_checked"] += row["terminal_trace_decisions_checked"]
        counts["terminal_returns_verified"] += row["terminal_return_verified"]
        counts["no_hero_decision_hands"] += row["terminal_trace_decisions_checked"] == 0
        if not row["diverged"]:
            counts["no_divergence_hands"] += 1
            require(row["residual_bb"] == 0, "Unchanged trajectory has nonzero residual")
        else:
            counts[row["control_variate_kind"]] += 1
            kind = "action_and_rng" if row["action_divergence"] and row["rng_divergence"] else (
                "action_only" if row["action_divergence"] else "rng_only")
            counts[f"first_divergence_{kind}"] += 1
            if row.get("actual_terminal_baseline_verified"):
                counts["bb_actual_fold_equals_baseline" if row["control_variate_kind"] == "bb_minraise_fold"
                       else "river_actual_passive_equals_baseline"] += 1
    arms = {}
    kinds = sorted({row["control_variate_kind"] for row in rows})
    for arm in ARMS:
        selected = [row for row in rows if row["arm"] == arm]
        require(len(selected) == expected_hands, f"{arm}: incomplete all-hand denominator")
        require(len({(row["job"], row["hand"]) for row in selected}) == len(selected),
                f"{arm}: duplicate completed-hand identity")
        parts = {kind: {"hands": sum(row["control_variate_kind"] == kind for row in selected),
                        "contribution_full_hand_denominator": estimate([
                            row["residual_bb"] if row["control_variate_kind"] == kind else 0.0 for row in selected])}
                 for kind in kinds}
        arms[arm] = {metric: estimate([row[field] for row in selected]) for metric, field in (
            ("raw", "actual_net_bb"), ("already_adjusted", "already_adjusted_net_bb"),
            ("residual", "residual_bb"), ("baseline", "control_variate_bb"))}
        arms[arm]["kinds"] = parts
    differences = {metric: difference(arms["candidate"][metric], arms["control"][metric])
                   for metric in ("raw", "already_adjusted", "residual", "baseline")}
    return dict(counts), arms, differences


def load_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    require(spec is not None and spec.loader is not None, f"Cannot load offline module: {path}")
    module = importlib.util.module_from_spec(spec)
    # Register before execution for dataclasses and load these exact dependency
    # bytes on each analysis, rather than accepting an unrelated cached import.
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def analyze(args: argparse.Namespace) -> dict:
    source = args.poker_source.resolve()
    sources = {name: fingerprint(source / name) for name in SUPPORT_SOURCES}
    analyzer_hash = fingerprint(Path(__file__))
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes, object_pairs_hook=unique_object)
    identities = arm_identities(manifest)
    pairs = confirmatory_spec(manifest, analyzer_hash, sources) if args.mode == "confirmatory" else []
    # Explicit sibling loads also make imported, repeatedly called analysis obey
    # the same source binding as a fresh command-line process.
    evaluation = load_module("eval", source / "eval.py")
    load_module("poker_audit", source / "poker_audit.py")
    load_module("slumbot_series", source / "slumbot_series.py")
    audit_module = load_module("first_divergence_archive_audit", source / "audit_archived_series.py")
    provenance = audit_module.analyze_archive(args.manifest, args.report_root,
                                             args.recorded_report_root, args.expected_manifest_sha256)
    require(provenance["all_planned_jobs_complete_and_eligible"],
            "Archived series failed execution-bound eligibility: " + json.dumps(provenance["eligibility_blockers"]))
    require(hashlib.sha256(manifest_bytes).hexdigest() == provenance["manifest_sha256"],
            "Manifest changed between frozen specification and archive verification")
    require(not args.output.exists(), f"Output already exists; refusing to overwrite evidence: {args.output}")
    library_overrides = {"control": args.control_library, "candidate": args.candidate_library}
    policies = {}
    for arm, identity in identities.items():
        path = (library_overrides[arm] or Path(identity["recorded_library"])).resolve()
        require(fingerprint(path) == identity["library_sha256"], f"Frozen {arm} library SHA256 mismatch")
        identity["resolved_library"] = str(path)
        policies[arm] = evaluation.Native(players=2, library=path, samples=identity["equity_samples"])
    settlement = evaluation.Native(players=2, library=Path(identities["control"]["resolved_library"]))
    audited_jobs = {job["id"]: job for job in provenance["jobs"]}
    records_by_job, signatures = {}, {}
    for job in manifest["jobs"]:
        audited = audited_jobs[job["id"]]
        records = read_log(Path(audited["resolved_output"]), audited["sha256"])
        completed = [row for row in records if "winnings_chips" in row]
        require(len(completed) == job["quota"] and [row.get("hand") for row in completed] == list(range(job["quota"])),
                f"{job['id']}: missing, duplicated, or nonsequential completed-hand coverage")
        if pairs:
            signatures[job["id"]] = seeded_log_signature(job, records)
        records_by_job[job["id"]] = completed
    for left, right in pairs:
        require(signatures[left] == signatures[right],
                f"Paired jobs differ in reservation, game, seed, or seat coverage: {left}, {right}")
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output / "hands.jsonl"
    rows, jobs = [], []
    with output.open("x") as sink:
        for job in manifest["jobs"]:
            selected = []
            for hand in records_by_job[job["id"]]:
                try:
                    row = analyze_hand(evaluation, policies, settlement, identities, job, hand)
                except (ValueError, KeyError, TypeError, AssertionError, RuntimeError) as exc:
                    raise AnalysisError(f"{job['id']}, hand {hand.get('hand')}: {exc}") from exc
                selected.append(row)
                sink.write(json.dumps(row, allow_nan=False) + "\n")
            rows.extend(selected)
            jobs.append({"id": job["id"], "arm": job["arm"],
                         "path": audited_jobs[job["id"]]["resolved_output"],
                         "sha256": audited_jobs[job["id"]]["sha256"],
                         "raw": estimate([row["actual_net_bb"] for row in selected]),
                         "residual": estimate([row["residual_bb"] for row in selected]),
                         "divergent_hands": sum(row["diverged"] for row in selected)})
            print(f"{job['id']}: checked {len(selected)} whole hands", flush=True)
    counts, arms, differences = summarize(rows, manifest["hands_per_arm"])
    # Bind reads to the audited bytes both before and after replay.
    require(fingerprint(args.manifest) == provenance["manifest_sha256"], "Manifest changed during replay")
    require(fingerprint(args.manifest.with_suffix(".execution.jsonl")) == provenance["execution"]["sha256"],
            "Execution record changed during replay")
    for audited in provenance["jobs"]:
        require(fingerprint(Path(audited["resolved_output"])) == audited["sha256"], "Log changed during replay")
    for identity in identities.values():
        require(fingerprint(Path(identity["resolved_library"])) == identity["library_sha256"], "Library changed during replay")
    require(fingerprint(Path(__file__)) == analyzer_hash and
            all(fingerprint(source / name) == digest for name, digest in sources.items()),
            "Offline analyzer/replay source changed during analysis")
    confirmation = args.mode == "confirmatory"
    low, high = differences["residual"]["ci95_bb100"]
    conclusion = ("improvement_supported" if low > 0 else "deterioration_supported" if high < 0 else "difference_unresolved")
    summary = {"schema": 1, "analysis": VERSION, "baseline_rule": BASELINE_RULE,
               "phase": "frozen_confirmation" if confirmation else "retrospective_development_diagnostic",
               "not_confirmatory": not confirmation,
               "conclusion": conclusion if confirmation else "historical_diagnostic_not_confirmatory",
               "independent_hand_seed_schedule_verified": confirmation,
               "historical_cross_hand_rng_distribution_not_explicitly_coupled": not confirmation,
               "all_planned_hands_included": True, "counts": counts, "arms": arms,
               "differences": differences, "jobs": jobs, "policy_identities": identities,
               "manifest_sha256": provenance["manifest_sha256"], "analyzer_sha256": analyzer_hash,
               "replay_source_hashes": sources, "per_hand_sha256": fingerprint(output),
               "method": "First action OR post-decision RNG divergence; one pre-action baseline per whole hand; all hands and zero residuals retained.",
               "coverage": "Both policy actions/RNG transitions replayed only up to and including first divergence. All actual action traces and terminal settlements checked separately; later policy actions are not replayed.",
               "ci_method": "Approximate normal 95% independent-hand, independent-arm interval. Matched hero seeds are not duplicate server deals. Residual arm means are not absolute poker win rates.",
               "limits": "The policy-value identity assumes equal deal and opponent conditional laws across arms and stateless policies beyond the recorded RNG. Seed/provenance checks cannot establish server stationarity. Historical diagnostics cannot replace the original primary result."}
    for name, value in (("provenance.json", provenance), ("summary.json", summary)):
        with (args.output / name).open("x") as stream:
            json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--report-root", type=Path, required=True)
    parser.add_argument("--mode", choices=("historical-diagnostic", "confirmatory"), required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory; existing evidence is never overwritten")
    parser.add_argument("--poker-source", type=Path, default=Path(__file__).resolve().parent,
                        help="Directory containing eval.py and the offline audit modules")
    parser.add_argument("--control-library", type=Path)
    parser.add_argument("--candidate-library", type=Path)
    parser.add_argument("--recorded-report-root")
    parser.add_argument("--expected-manifest-sha256")
    args = parser.parse_args(argv)
    try:
        result = analyze(args)
        print(json.dumps({"phase": result["phase"], "conclusion": result["conclusion"],
                          "counts": result["counts"], "differences": result["differences"],
                          "summary": str((args.output / "summary.json").resolve())}, indent=2, allow_nan=False))
        return 0
    except (OSError, ValueError, KeyError, TypeError, AssertionError, RuntimeError) as exc:
        print(f"first-divergence analysis error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
