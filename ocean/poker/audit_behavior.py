"""Compare native policies on identical six-seat decision states, offline.

This measures action differences on a sampled corpus, not win rate or human
similarity. A second independent seed measures each policy's own randomness.
"""
import argparse
from collections import Counter
import hashlib
import itertools
import json
from pathlib import Path
import random

from eval import Native, ROOT


NAMES = {
    0: "initial_rules", 1: "original_rules", 2: "positional_rules",
    3: "broad_defense", 4: "fullhouse", 5: "focused_defense",
    6: "uniform_equity_32", 7: "range_equity_32", 8: "range_equity_128",
}
FIELDS = {
    "original": [1] * 6, "fullhouse": [4] * 6,
    "range32": [7] * 6, "range128": [8] * 6,
    "mixed": [0, 2, 4, 5, 7, 8],
}


def action_kind(action, call):
    return "fold" if action < 0 else "raise" if action > 0 else "call" if call else "check"


def accumulator(styles):
    return {
        "states": 0, "facing_bet_states": 0, "can_raise_states": 0,
        "policies": {str(s): {"actions": Counter(), "facing_bet_folds": 0,
            "self_exact_disagreements": 0, "self_kind_disagreements": 0,
            "raise_pot_sum": 0.0} for s in styles},
        "pairs": {f"{a}:{b}": {"exact_disagreements": 0, "kind_disagreements": 0}
            for a, b in itertools.combinations(styles, 2)},
    }


def record(group, actions, repeats, call, can_raise, high, pot):
    group["states"] += 1
    group["facing_bet_states"] += bool(call)
    group["can_raise_states"] += can_raise
    for style, action in actions.items():
        row = group["policies"][str(style)]
        kind = action_kind(action, call)
        row["actions"][kind] += 1
        row["facing_bet_folds"] += bool(call and action < 0)
        row["self_exact_disagreements"] += action != repeats[style]
        row["self_kind_disagreements"] += kind != action_kind(repeats[style], call)
        if action > 0:
            row["raise_pot_sum"] += (action - high) / (pot + call)
    for a, b in itertools.combinations(actions, 2):
        row = group["pairs"][f"{a}:{b}"]
        row["exact_disagreements"] += actions[a] != actions[b]
        row["kind_disagreements"] += action_kind(actions[a], call) != action_kind(actions[b], call)


def finish(group):
    n = group["states"]
    if not n:
        return
    for row in group["policies"].values():
        for kind in ("fold", "check", "call", "raise"):
            row["actions"].setdefault(kind, 0)
        assert sum(row["actions"].values()) == n
        row["action_percent"] = {k: 100 * v / n for k, v in row["actions"].items()}
        row["fold_when_facing_bet_percent"] = (100 * row["facing_bet_folds"]
            / group["facing_bet_states"] if group["facing_bet_states"] else None)
        raises = row["actions"]["raise"]
        row["mean_raise_pot_fraction"] = row["raise_pot_sum"] / raises if raises else None
        for key in ("exact", "kind"):
            row[f"self_{key}_disagreement_percent"] = 100 * row[f"self_{key}_disagreements"] / n
    for row in group["pairs"].values():
        for key in ("exact", "kind"):
            row[f"{key}_disagreement_percent"] = 100 * row[f"{key}_disagreements"] / n


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path,
                        default=ROOT / "build/poker/equity_v2/libpoker_fullhouse6.so")
    parser.add_argument("--hands-per-field", type=int, default=400)
    parser.add_argument("--seed", type=int, default=202610181)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.hands_per_field < 1:
        parser.error("--hands-per-field must be positive")
    if args.output.exists():
        parser.error("output already exists; choose a new path")
    native = Native(players=6, seed=args.seed, library=args.library)
    styles = list(NAMES)
    if any(not (native.styles & (1 << s)) for s in styles):
        parser.error("this audit requires a six-seat library containing policies 0 through 8")
    groups = {key: accumulator(styles) for key in
              ("all", "preflop", "postflop", "postflop_multiway")}
    field_counts = {}
    coverage = Counter()
    corpus = hashlib.sha256()
    action_digest = hashlib.sha256()
    checks = 0
    for field_index, (field, seats) in enumerate(FIELDS.items()):
        deals = random.Random(args.seed + 1000003 * field_index)
        probes = random.Random(args.seed + 2000003 * (field_index + 1))
        generation = random.Random(args.seed + 3000017 * (field_index + 1))
        seat_rngs = [generation.getrandbits(64) or 1 for _ in seats]
        field_counts[field] = {"hands": 0, "preflop_states": 0, "postflop_states": 0}
        for hand in range(args.hands_per_field):
            native.reset([20000] * 6, deals.sample(range(52), 17), hand % 6)
            steps = 0
            while True:
                state, rows, board = native.snapshot()
                actor, street, pot, high = state[:4]
                if actor == 255:
                    break
                hero = rows[actor]
                call = min(high - hero[3], hero[0])
                legal = native.actions()
                can_raise = any(a > 0 for a in legal)
                maximum = hero[3] + hero[0]
                public = {"field": field, "hand": hand, "state": state,
                    "players": [r[:5] for r in rows], "hole": hero[5:7],
                    "board": board[:street + 2] if street else []}
                corpus.update(json.dumps(public, sort_keys=True).encode() + b"\n")
                before = bytes(native.storage)
                actions, repeats = {}, {}
                for draws in (actions, repeats):
                    seed = probes.getrandbits(64) or 1
                    for style in styles:
                        native.rng.value = seed
                        action = native.bot(style)
                        if action > 0:
                            assert can_raise and high < action <= maximum
                            assert action >= high + state[4] or action == maximum
                        else:
                            assert action in legal
                        draws[style] = action
                        checks += 1
                assert bytes(native.storage) == before, "policy mutated the decision state"
                # All three equity policies delegate preflop to focused defense.
                if not street:
                    for draws in (actions, repeats):
                        assert draws[5] == draws[6] == draws[7] == draws[8]
                action_digest.update(json.dumps([actions, repeats], sort_keys=True).encode() + b"\n")
                live = state[5].bit_count()
                phase = "postflop" if street else "preflop"
                keys = ["all", phase]
                if street and live >= 3:
                    keys.append("postflop_multiway")
                for key in keys:
                    record(groups[key], actions, repeats, call, can_raise, high, pot)
                coverage[f"street{street}_live{live}"] += 1
                field_counts[field][f"{phase}_states"] += 1
                # Independent persistent per-seat RNGs generate the trajectories;
                # counterfactual probes never affect dealing or played actions.
                native.rng.value = seat_rngs[actor]
                played = native.bot(seats[actor])
                seat_rngs[actor] = native.rng.value
                native.step(played)
                steps += 1
                assert steps < 4096
            final, final_rows, _ = native.snapshot()
            assert final[2] == 0 and sum(r[0] for r in final_rows) == 6 * 20000
            field_counts[field]["hands"] += 1
        print(f"{field}: {field_counts[field]}", flush=True)
    for group in groups.values():
        finish(group)
    result = {
        "schema": 1, "seed": args.seed, "players": 6, "starting_bb": 200,
        "history": False, "hands_per_field": args.hands_per_field,
        "hands_total": args.hands_per_field * len(FIELDS), "policy_names": NAMES,
        "fields": FIELDS, "field_counts": field_counts, "coverage": coverage,
        "library": str(args.library),
        "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
        "audit_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "corpus_sha256": corpus.hexdigest(), "action_sha256": action_digest.hexdigest(),
        "legal_probe_actions_checked": checks, "groups": groups,
        "method": [
            "Independent 200-BB deals under five fixed six-seat policy fields; all decision states included.",
            "Each policy sees the identical state and same starting RNG seed per probe; two probes per state.",
            "Pair differences and action frequencies use the first probe; self differences compare independent seeds.",
            "Random draws are coupled by seed, but different algorithms may consume or interpret them differently.",
            "Exact differences include bet size; kind differences compare fold/check/call/raise only.",
            "Postflop multiway means at least three live players, including players already all-in.",
            "Counterfactual action frequencies are not trajectory VPIP/PFR or a strength/human-realism score.",
            "Results describe this corpus; no independence, population confidence interval or personality claim.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as output:
        json.dump(result, output, indent=2)
        output.write("\n")
    print(f"Saved {groups['all']['states']} states / {checks} legal probes to {args.output}")


if __name__ == "__main__":
    main()
