"""Thin native-bot/Slumbot runner. HTTP and text parsing stay outside training."""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import time

ROOT = Path(__file__).resolve().parents[2]
RANKS, SUITS = "23456789TJQKA", "cdhs"


def card(text):
    return 4 * RANKS.index(text[0]) + SUITS.index(text[1])


def text_card(value):
    return RANKS[value >> 2] + SUITS[value & 3]


class Native:
    def __init__(self, players=2, seed=73, library=None, samples=0):
        self.players = players
        if samples not in (0, 32, 64, 128):
            raise ValueError("Equity samples must be 0 (build default), 32, 64 or 128")
        self.samples = samples
        self.path = Path(library) if library else ROOT / f"build/poker/libpoker{players}.so"
        self.lib = C.CDLL(str(self.path.resolve()))
        self.styles = self.lib.poker_bot_styles() if hasattr(self.lib, "poker_bot_styles") else 7
        self.lib.poker_bytes.restype = C.c_size_t
        signatures = {
            "poker_seed": [C.c_void_p, C.c_uint64],
            "poker_reset": [C.c_void_p, C.c_int, C.c_int, C.POINTER(C.c_uint32),
                            C.POINTER(C.c_uint8)],
            "poker_step": [C.c_void_p, C.c_int, C.c_int32],
            "poker_snapshot": [C.c_void_p, C.c_int, C.POINTER(C.c_uint32)],
            "poker_actions": [C.c_void_p, C.c_int, C.POINTER(C.c_int32)],
            "poker_observe": [C.c_void_p, C.c_int, C.c_int, C.POINTER(C.c_float)],
            "poker_bot": [C.c_void_p, C.c_int, C.POINTER(C.c_uint64), C.c_int],
            "poker_rank": [C.POINTER(C.c_uint8), C.c_int],
        }
        for name, args in signatures.items():
            getattr(self.lib, name).argtypes = args
        if hasattr(self.lib, "poker_decide"):
            self.lib.poker_decide.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_uint64),
                                             C.c_int, C.c_int, C.c_void_p]
        elif samples:
            raise ValueError("This archived library does not support a sample-budget setting")
        self.lib.poker_rank.restype = C.c_uint32
        self.storage = C.create_string_buffer(self.lib.poker_bytes())
        self.rng = C.c_uint64(seed or 1)
        self.lib.poker_seed(self.storage, seed)

    def reset(self, stacks, deck, button=None):
        button = self.players - 1 if button is None else button
        self.lib.poker_reset(self.storage, 0, button,
                            (C.c_uint32 * self.players)(*stacks),
                            (C.c_uint8 * len(deck))(*deck))

    def snapshot(self):
        out = (C.c_uint32 * (15 + 7 * self.players))()
        self.lib.poker_snapshot(self.storage, 0, out)
        rows = [list(out[10 + 7 * p:17 + 7 * p]) for p in range(self.players)]
        return list(out[:10]), rows, list(out[-5:])

    def actions(self):
        out = (C.c_int32 * 8)()
        mask = self.lib.poker_actions(self.storage, 0, out)
        return [out[i] for i in range(8) if mask & (1 << i)]

    def step(self, action):
        self.lib.poker_step(self.storage, 0, action)

    def bot(self, style):
        if not self.styles & (1 << style):
            raise ValueError(f"Policy style {style} is unavailable in {self.path}")
        if self.samples:
            return self.lib.poker_decide(self.storage, 0, C.byref(self.rng), style, self.samples, None)
        return self.lib.poker_bot(self.storage, 0, C.byref(self.rng), style)


def replay(native, response):
    hero = response["client_pos"]
    own = [card(c) for c in response["hole_cards"]]
    board = [card(c) for c in response["board"]]
    other = [card(c) for c in response.get("slumbot_hole_cards", [])]
    available = iter(c for c in range(52) if c not in own + board + other)
    other = other or [next(available), next(available)]
    board += [next(available) for _ in range(5 - len(board))]
    holes = other + own if hero else own + other
    native.reset([20000, 20000], holes + board)
    history = response["action"]
    tokens = re.findall(r"b\d+|[kcf/]", history)
    assert "".join(tokens) == history
    for token in tokens:
        if token == "/":
            continue
        assert native.snapshot()[0][0] != 255
        native.step(int(token[1:]) if token[0] == "b" else -1 if token == "f" else 0)
    return native.snapshot()


def slumbot(args):
    from concurrent.futures import ThreadPoolExecutor
    from queue import Queue
    import requests
    import signal
    from threading import Event

    assert 1 <= args.games <= 50 and args.hands > 0 and args.request_delay >= 0
    binary = Path(args.library) if args.library else ROOT / "build/poker/libpoker2.so"
    samples = getattr(args, "equity_samples", 0)
    if not Native(library=binary, samples=samples).styles & (1 << args.style):
        raise ValueError(f"Policy style {args.style} is unavailable in {binary}")
    agent = "PufferTank-PokerEval/1.0" + (f" (+{args.contact})" if args.contact else "")
    metadata = {"style": args.style, "equity_samples": samples,
        "seed": args.seed, "hands_requested": args.hands,
        "library_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "opponent": "slumbot.com", "starting_bb": 200,
        "request_delay_seconds": args.request_delay, "concurrent_games": args.games,
        "user_agent": agent, "log_schema": 2}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    progress = Path(args.progress) if args.progress else output.with_suffix(".progress.txt")
    results, rngs = [], {}
    unresolved = 0
    if args.resume:
        records = [json.loads(line) for line in output.read_text().splitlines()]
        original = records[0]["metadata"]
        assert all(original[k] == metadata[k] for k in ("style", "library_sha256"))
        assert original.get("equity_samples", 0) == samples
        hands = [r for r in records if "winnings_chips" in r]
        results = [r["winnings_chips"] / 100 for r in hands]
        unresolved = sum("interrupted" in r for r in records)
        rngs = {r.get("game", 0): r["policy_rng"] for r in hands if "policy_rng" in r}
    initial = len(results)
    assert initial <= args.hands
    started = time.monotonic()
    events, stop = Queue(), Event()

    def play(game, quota):
        session = requests.Session()
        session.headers["User-Agent"] = agent
        native = Native(seed=args.seed + game * 1000003, library=binary, samples=samples)
        if game in rngs:
            native.rng.value = rngs[game]
        token, failure = None, None
        completed = failures = 0

        def backoff(response, attempt):
            delay = min(60, 2 ** attempt)
            if response is not None and response.headers.get("Retry-After"):
                from email.utils import parsedate_to_datetime
                value = response.headers["Retry-After"]
                try:
                    requested = float(value)
                except ValueError:
                    requested = parsedate_to_datetime(value).timestamp() - time.time()
                delay = max(delay, requested)
            events.put(("backoff", {"game": game, "seconds": delay,
                "status": response.status_code if response is not None else None}))
            stop.wait(delay)

        def post(endpoint, data):
            for attempt in range(6):
                if args.request_delay:
                    time.sleep(args.request_delay)
                response = session.post(f"https://slumbot.com/slumbot/api/{endpoint}",
                    json=data, timeout=60)
                if response.status_code == 429 and attempt < 5:
                    backoff(response, attempt)
                    if not stop.is_set():
                        continue
                response.raise_for_status()
                return response.json()

        try:
            while completed < quota and not stop.is_set():
                decisions, observed = [], {}
                try:
                    stage = "new_hand"
                    data = post("new_hand", {"token": token} if token else {})
                    while True:
                        if "error_msg" in data:
                            raise RuntimeError("Slumbot returned error_msg")
                        token = data.get("token", token)
                        for key in ("hole_cards", "board", "slumbot_hole_cards",
                                "session_num_hands", "session_total"):
                            if key in data:
                                observed[key] = data[key]
                        if "winnings" in data:
                            break
                        state, rows, _ = replay(native, data)
                        assert state[0] == data["client_pos"], (state, data["action"])
                        rng_before = native.rng.value
                        action = native.bot(args.style)
                        if action < 0:
                            move = "f"
                        elif action:
                            move = f"b{action}"
                        else:
                            move = "c" if state[3] > rows[state[0]][3] else "k"
                        hero = rows[state[0]]
                        # Only decision-time visible cards enter this trace.
                        # Opponent revelations belong to the terminal record.
                        decisions.append({"client_pos": data["client_pos"],
                            "hole_cards": list(data["hole_cards"]),
                            "board": list(data["board"]), "action": data["action"],
                            "street": state[1], "pot_chips": state[2],
                            "call_chips": min(state[3] - hero[3], hero[0]),
                            "stack_chips": hero[0], "street_commitment_chips": hero[3],
                            "high_bet_chips": state[3], "full_raise_increment_chips": state[4],
                            "legal_abstract_amounts": native.actions(),
                            "sent": move, "native_action": action,
                            "policy_rng_before": rng_before, "policy_rng_after": native.rng.value})
                        stage = "act"
                        data = post("act", {"token": token, "incr": move})
                    events.put(("result", {"game": game, "position": data["client_pos"],
                        "winnings_chips": data["winnings"], "action": data["action"],
                        "policy_rng": native.rng.value, **observed, "decisions": decisions}))
                    completed += 1
                    failures = 0
                except (requests.RequestException, RuntimeError) as error:
                    # A failed act may already have been applied. Record it,
                    # then back off and start a new session without replaying it.
                    response = getattr(error, "response", None)
                    status = response.status_code if response is not None else None
                    event = {"game": game, "type": type(error).__name__,
                        "stage": stage, "game_hand": completed, "status": status,
                        **observed, "decisions": decisions}
                    body = {}
                    if response is not None:
                        try:
                            body = response.json()
                        except ValueError:
                            pass
                    elif isinstance(error, RuntimeError):
                        body = data
                    message = body.get("error_msg", body.get("error"))
                    if message is None and isinstance(error, requests.RequestException):
                        message = str(error)
                    if isinstance(message, str):
                        event["message"] = message.replace(token or "<none>", "<token>")[:500]
                    if stage == "act":
                        event.update(action_before=data.get("action"),
                            client_pos=data.get("client_pos"), sent=move)
                    events.put(("interrupted", event))
                    transient = isinstance(error, (requests.ConnectionError, requests.Timeout))
                    transient |= status in (500, 502, 503, 504)
                    failures += 1
                    if not transient or failures >= 6:
                        failure = event
                        stop.set()
                        break
                    backoff(response, failures - 1)
                    token = None
        finally:
            session.close()
        return failure

    active = 0

    def report():
        elapsed = time.monotonic() - started
        rate = (len(results) - initial) / elapsed if elapsed else 0
        eta = (args.hands - len(results)) / rate if rate else None
        summary = {**metadata, "hands": len(results),
            "target_reached": len(results) == args.hands,
            "unresolved_hands": unresolved, "net_bb": sum(results),
            "bb100": 100 * statistics.mean(results) if results else None,
            "ci95_half_width_bb100": 196 * statistics.stdev(results) / math.sqrt(len(results))
                if len(results) > 1 else None,
            "seconds_this_process": elapsed, "hands_per_second": rate,
            "eta_seconds": eta, "active_games": active}
        if unresolved:
            count = len(results) + unresolved
            summary["bb100_bounds_for_unresolved"] = [
                100 * (sum(results) + direction * 200 * unresolved) / count
                for direction in (-1, 1)]
        eta_text = f"{eta / 60:.1f} min" if eta is not None else "measuring"
        line = (f"hands={len(results)}/{args.hands} net_bb={sum(results):.2f} "
            f"bb100={summary['bb100'] or 0:.2f} active_games={active} "
            f"unresolved={unresolved} eta={eta_text}")
        print(line, flush=True)
        progress.write_text(time.strftime("%Y-%m-%d %H:%M:%S UTC\n", time.gmtime())
            + line + "\n")
        return summary

    # Graceful signals stop between hands. A forced SIGKILL cannot drain; logs
    # without a final summary must be treated as having unknown pending outcomes.
    previous = signal.signal(signal.SIGINT, lambda *_: stop.set())
    previous_term = signal.signal(signal.SIGTERM, lambda *_: stop.set())
    failures = []
    try:
        with output.open("a" if args.resume else "x") as log:
            log.write(json.dumps({"resumed" if args.resume else "metadata": metadata}) + "\n")
            log.flush()
            pairs, odd = divmod(args.hands - initial, 2)
            with ThreadPoolExecutor(max_workers=args.games) as pool:
                jobs = {}
                for game in range(args.games):
                    quota = 2 * (pairs // args.games + (game < pairs % args.games))
                    quota += odd if game == 0 else 0
                    if quota:
                        jobs[pool.submit(play, game, quota)] = game
                active = len(jobs)
                for job in jobs:
                    job.add_done_callback(lambda job: events.put(("done", job)))
                report()
                while active:
                    kind, data = events.get()
                    if kind == "done":
                        active -= 1
                        try:
                            failure = data.result()
                        except Exception as error:
                            failure = {"game": jobs[data], "type": type(error).__name__}
                            stop.set()
                        if failure:
                            failures.append(failure)
                        continue
                    if kind == "result":
                        data["hand"] = len(results)
                        results.append(data["winnings_chips"] / 100)
                        log.write(json.dumps(data) + "\n")
                    else:
                        unresolved += kind == "interrupted"
                        log.write(json.dumps({kind: data}) + "\n")
                    log.flush()
                    if kind == "result" and len(results) % 25 == 0:
                        report()
            summary = report()
            summary["complete"] = summary["target_reached"] and not unresolved and not failures
            summary["failures"] = failures
            log.write(json.dumps({"summary": summary}) + "\n")
            print(json.dumps(summary), flush=True)
    finally:
        signal.signal(signal.SIGINT, previous)
        signal.signal(signal.SIGTERM, previous_term)
    if failures:
        raise SystemExit(f"Slumbot evaluation stopped: {failures}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--hands", type=int, default=100)
    policies = parser.add_mutually_exclusive_group()
    policies.add_argument("--policy", choices=("rules", "fullhouse", "range"),
                          help="Active policy (default: range)")
    policies.add_argument("--style", type=int, choices=range(9),
                          help="Numeric ID, including archived experiments when supported by --library")
    parser.add_argument("--equity-samples", type=int, choices=(0, 32, 64, 128), default=0,
                        help="Range policy sampling budget; 0 uses the pinned build default")
    parser.add_argument("--seed", type=int, default=73)
    parser.add_argument("--output", required=True)
    parser.add_argument("--library")
    parser.add_argument("--request-delay", type=float, default=0)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--games", type=int, default=1)
    parser.add_argument("--contact")
    parser.add_argument("--progress")
    args = parser.parse_args()
    if args.style is None:
        args.style = {"rules": 1, "fullhouse": 4, "range": 7}[args.policy or "range"]
    if args.equity_samples and args.style != 7:
        parser.error("--equity-samples applies to --policy range / --style 7")
    slumbot(args)
