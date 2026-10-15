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
    def __init__(self, players=2, seed=73, library=None):
        self.players = players
        self.path = Path(library) if library else ROOT / f"build/poker/libpoker{players}.so"
        self.lib = C.CDLL(str(self.path.resolve()))
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
    import requests
    session = requests.Session()
    native = Native(seed=args.seed, library=args.library)
    token = None
    results = []
    started = time.monotonic()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    binary = native.path
    metadata = {"style": args.style, "seed": args.seed, "hands_requested": args.hands,
                "library_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "opponent": "slumbot.com", "starting_bb": 200}
    with output.open("x") as log:
        log.write(json.dumps({"metadata": metadata}) + "\n")
        failure = None
        try:
            for hand in range(args.hands):
                stage = "new_hand"
                response = session.post("https://slumbot.com/slumbot/api/new_hand",
                    json={"token": token} if token else {}, timeout=60)
                response.raise_for_status()
                data = response.json()
                while True:
                    if "error_msg" in data:
                        raise RuntimeError("Slumbot returned error_msg")
                    token = data.get("token", token)
                    if "winnings" in data:
                        break
                    state, rows, _ = replay(native, data)
                    assert state[0] == data["client_pos"], (state, data["action"])
                    action = native.bot(args.style)
                    if action < 0:
                        move = "f"
                    elif action:
                        move = f"b{action}"
                    else:
                        move = "c" if state[3] > rows[state[0]][3] else "k"
                    stage = "act"
                    response = session.post("https://slumbot.com/slumbot/api/act",
                        json={"token": token, "incr": move}, timeout=60)
                    response.raise_for_status()
                    data = response.json()
                result = {"hand": hand, "position": data["client_pos"],
                    "winnings_chips": data["winnings"], "action": data["action"]}
                log.write(json.dumps(result) + "\n")
                log.flush()
                results.append(data["winnings"] / 100)
                if (hand + 1) % 25 == 0:
                    print(f"style={args.style} hands={hand + 1} "
                        f"bb100={100 * statistics.mean(results):.2f}", flush=True)
        except (requests.RequestException, RuntimeError) as error:
            # An act timeout may occur after the server applied the action.
            # Preserve completed hands; never retry or invent the missing result.
            failure = {"type": type(error).__name__, "stage": stage, "hand": hand}
            log.write(json.dumps({"interrupted": failure}) + "\n")
        error = (1.96 * statistics.stdev(results) / math.sqrt(len(results))
                 if len(results) > 1 else None)
        summary = {**metadata, "hands": len(results), "complete": failure is None,
                   "bb100": 100 * statistics.mean(results) if results else None,
                   "ci95_half_width_bb100": None if error is None else 100 * error,
                   "seconds": time.monotonic() - started}
        log.write(json.dumps({"summary": summary}) + "\n")
        print(json.dumps(summary), flush=True)
    if failure:
        raise SystemExit(f"Slumbot evaluation incomplete: {failure}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--hands", type=int, default=100)
    parser.add_argument("--style", type=int, choices=(0, 1, 2), default=1)
    parser.add_argument("--seed", type=int, default=73)
    parser.add_argument("--output", required=True)
    parser.add_argument("--library")
    slumbot(parser.parse_args())
