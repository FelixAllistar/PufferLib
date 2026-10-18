"""Offline audit of recorded Slumbot actions and frozen-bot decision probes."""
import argparse
from collections import Counter, defaultdict
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "ocean/poker"))
from eval import Native, replay

STREETS = ("preflop", "flop", "turn", "river")


def analyze(log, library):
    records = [json.loads(line) for line in log.read_text().splitlines()]
    hands = [r for r in records if "winnings_chips" in r]
    original = records[0]["metadata"]
    digest = hashlib.sha256(library.read_bytes()).hexdigest()
    assert original["library_sha256"] == digest
    native = Native(library=library)
    native.lib.poker_can_raise.argtypes = [C.c_void_p, C.c_int]
    native.lib.poker_can_raise.restype = C.c_int
    groups, positions = defaultdict(list), defaultdict(list)
    facing, folding = Counter(), Counter()
    actions, opportunities = Counter(), Counter()
    mismatches = []
    folds_checked = showdowns_checked = action_count = 0

    for hand in hands:
        hero = hand["position"]
        native.reset([20000, 20000], list(range(9)))
        committed, bets = [100, 50], [100, 50]
        tokens = re.findall(r"b\d+|[kcf/]", hand["action"])
        assert "".join(tokens) == hand["action"]
        voluntary = raised = False
        fold_by, fold_street = None, None
        raise_count = 0
        street_from_slashes = 0
        for token in tokens:
            if token == "/":
                street_from_slashes += 1
                bets = [0, 0]
                continue
            state, rows, _ = native.snapshot()
            actor, street, pot, high, increment = state[:5]
            assert actor in (0, 1), (hand["hand"], token, state)
            assert street == street_from_slashes, hand["hand"]
            assert pot == sum(committed), hand["hand"]
            assert [r[2] for r in rows] == committed, hand["hand"]
            assert [r[3] for r in rows] == bets, hand["hand"]
            call = high - bets[actor]
            maximum = bets[actor] + rows[actor][0]
            action = int(token[1:]) if token[0] == "b" else -1 if token == "f" else 0
            if token == "f":
                assert call > 0
                fold_by, fold_street = actor, street
            elif token == "k":
                assert call == 0
            elif token == "c":
                assert call > 0
            else:
                assert native.lib.poker_can_raise(native.storage, 0)
                assert high < action <= maximum
                assert action >= high + increment or action == maximum
            if actor == hero:
                actions[f"{STREETS[street]}_{token[0]}"] += 1
                if call:
                    facing[STREETS[street]] += 1
                    folding[STREETS[street]] += token == "f"
                    if street and call / (pot + call) <= 1 / 6:
                        opportunities["postflop_price_at_most_one_sixth"] += 1
                        opportunities["fold_at_price_at_most_one_sixth"] += token == "f"
                if street == 0:
                    voluntary |= token[0] in "bc"
                    raised |= token[0] == "b"
                    if raise_count == 1:
                        opportunities["face_preflop_raise"] += 1
                        opportunities["fold_to_preflop_raise"] += token == "f"
                        opportunities["three_bet"] += token[0] == "b"
            if token[0] == "b" and street == 0:
                raise_count += 1
            if token != "f":
                pay = min((action or high) - bets[actor], rows[actor][0])
                bets[actor] += pay
                committed[actor] += pay
            native.step(action)
            action_count += 1
        state, _, _ = native.snapshot()
        assert state[0] == 255, hand["hand"]
        winnings = hand["winnings_chips"]
        if fold_by is not None:
            expected = -committed[hero] if fold_by == hero else committed[1 - hero]
            if expected != winnings:
                mismatches.append({"hand": hand["hand"], "expected": expected,
                    "actual": winnings})
            folds_checked += 1
            who = "hero" if fold_by == hero else "slumbot"
            group = f"{who}_folds_{STREETS[fold_street]}"
        else:
            assert committed[0] == committed[1], hand["hand"]
            if winnings not in (-committed[hero], 0, committed[hero]):
                mismatches.append({"hand": hand["hand"], "showdown_stake": committed[hero],
                    "actual": winnings})
            showdowns_checked += 1
            group = "showdown_" + ("win" if winnings > 0 else "loss" if winnings < 0 else "tie")
        groups[group].append(winnings)
        positions[str(hero)].append(winnings)
        opportunities["vpip_hands"] += voluntary
        opportunities["preflop_raise_hands"] += raised

    def summarize(values):
        return {"hands": len(values), "net_bb": sum(values) / 100,
            "bb100": sum(values) / len(values) if values else None}

    probes = []
    for name, hole, board, history, hero in [
        ("nut_flush_draw_small_bet", ["Ah", "Kh"], ["Qh", "7h", "2c"],
            "b250b2000c/b100", 1),
        ("aces_dry_flop_pot_bet", ["As", "Ad"], ["Kh", "7c", "2d"],
            "b250c/b500", 1),
        ("ak_offsuit_vs_open", ["As", "Kd"], [], "b250", 0),
    ]:
        response = {"client_pos": hero, "hole_cards": hole, "board": board,
            "action": history}
        state, rows, _ = replay(native, response)
        assert state[0] == hero
        choices = Counter()
        for seed in range(1, 1001):
            native.rng.value = seed
            choices[str(native.bot(1))] += 1
        call = state[3] - rows[hero][3]
        probes.append({"name": name, **response, "pot_before_call_chips": state[2],
            "call_chips": call, "call_price": call / (state[2] + call),
            "actions_over_1000_rng_states": dict(choices)})

    return {"log": str(log.relative_to(ROOT)), "log_sha256": hashlib.sha256(
            log.read_bytes()).hexdigest(), "library_sha256": digest,
        "completed_hands": len(hands), "net_bb": sum(r["winnings_chips"] for r in hands) / 100,
        "coverage": {"hands_with_hole_cards": sum("hole_cards" in r for r in hands),
            "hands_with_board": sum("board" in r for r in hands)},
        "unresolved_attempts": sum("interrupted" in r for r in records),
        "audit": {"actions_legally_replayed": action_count,
            "fold_payoffs_checked": folds_checked,
            "showdown_stakes_checked": showdowns_checked, "mismatches": mismatches,
            "limit": "No historical cards: cannot verify showdown winners or individual decision EV."},
        "by_position": {k: summarize(v) for k, v in positions.items()},
        "by_outcome": {k: summarize(v) for k, v in sorted(groups.items())},
        "facing_bet": {street: {"decisions": facing[street], "folds": folding[street],
            "fold_percent": 100 * folding[street] / facing[street]} for street in STREETS},
        "hero_actions": dict(actions), "opportunities": dict(opportunities),
        "synthetic_frozen_bot_probes": probes,
        "largest_losses": sorted(hands, key=lambda r: r["winnings_chips"])[:10]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=ROOT /
        "reports/poker/20261015/slumbot_style1_seed20261016_paced.jsonl")
    parser.add_argument("--library", type=Path, default=ROOT / "build/poker/rules_v0.so")
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("analysis.json"))
    args = parser.parse_args()
    result = analyze(args.log.resolve(), args.library.resolve())
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
