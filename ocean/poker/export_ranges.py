#!/usr/bin/env python3
"""Offline, split-aware export for the single frozen root-range experiment.

The native policy state contains synthetic opponent cards. The revealed target
is passed separately and never changes the legal pair set or its weights.
Only TRAIN is permitted before its model artifact exists. New VAL requires
root's provenance-only release; TEST also requires a successful VAL and an
exact native/model/scorer choice freeze.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PLAN_SHA = "a781a93180793a84161b160612a1ab3f7d0a25479c07d1c4a42a4b0eac8da630"
SPLIT_SHA = "439744b6ffab208a2cdf8c9c88fe13d194a7d701ee7c37190a3e6065bee94a6d"
REPLAY_SHA = "9f6c584e0ab76c37d7df07fccefd1b3889769e4918c418a829ef4e7fc3353137"
SERIES_SHA = "349f87802679704366fee0f5ee7c49a9fc791ff3dd4b0496fecbf137d1b52cd9"
RELEASE_SHA = "53d12fb24b0a430df0912fec16c506b1afbb5a275dacf2317b719716bfb0a22c"
DIAGNOSTIC_ROWS_SHA = "623c696e25d8c91bc24d105123c150ab16ec30ede9e8b3c7bc6fd5d5164aa8ac"
PAIRS = 990


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    with Path(path).open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


def check_plan(path):
    if sha(path) != PLAN_SHA:
        raise ValueError("The exact frozen one-parameter plan is required")
    return json.loads(Path(path).read_text())


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def training_partition(salt, log_sha, hand):
    key = f"{salt}:{log_sha}:{hand}".encode()
    return int.from_bytes(hashlib.sha256(key).digest()[:8], "big") % 1000 < 700


def holdout_partition(job, reservation):
    if not isinstance(job, str) or ":" in job or not job.isascii():
        raise ValueError("Invalid frozen job identity")
    if type(reservation) is not int or reservation < 0:
        raise ValueError("Reservation must be a canonical nonnegative integer")
    key = f"poker-root-range-shrinkage-holdout-v1:{SERIES_SHA}:{job}:{reservation}"
    return "val" if hashlib.sha256(key.encode("ascii")).digest()[0] < 128 else "test"


def validate_choice(path, model_path, scorer_path):
    value = json.loads(Path(path).read_text())
    if value.get("phase") != "root_range_exact_choice_frozen_before_test":
        raise ValueError("TEST requires its explicit exact-choice freeze")
    if value.get("plan_sha256") != PLAN_SHA or value.get("model_sha256") != sha(model_path):
        raise ValueError("TEST model/plan differs from the frozen choice")
    if value.get("scorer_sha256") != sha(scorer_path):
        raise ValueError("TEST scorer differs from the frozen choice")
    if value.get("exporter_sha256") != sha(Path(__file__)):
        raise ValueError("TEST exporter differs from the frozen choice")
    if not isinstance(value.get("native_library_sha256"), str) or len(value["native_library_sha256"]) != 64:
        raise ValueError("TEST native choice is missing")
    validation = Path(value["validation_report_path"])
    if sha(validation) != value["validation_report_sha256"]:
        raise ValueError("Changed VAL report")
    report = json.loads(validation.read_text())
    if report.get("partition") != "val" or report.get("all_nine_passed") is not True:
        raise ValueError("TEST cannot rescue an unsuccessful VAL")
    if report.get("model_sha256") != sha(model_path):
        raise ValueError("VAL and TEST model differ")
    return value


def sources(args):
    if args.partition == "train":
        if sha(args.split) != SPLIT_SHA:
            raise ValueError("Changed original TRAIN split")
        value = json.loads(args.split.read_text())
        return value["files"], value["salt"], None
    if args.model is None or args.release is None:
        raise ValueError("VAL requires a frozen TRAIN model and root's source-only release")
    model = json.loads(args.model.read_text())
    if model.get("plan_sha256") != PLAN_SHA or model.get("training_partition") != "train":
        raise ValueError("Only the single frozen old-TRAIN model is permitted")
    if args.partition == "test":
        if args.choice is None:
            raise ValueError("TEST is blocked until VAL and the native choice are frozen")
        validate_choice(args.choice, args.model, HERE / "fit_root_range.py")
    if sha(args.release) != RELEASE_SHA:
        raise ValueError("Root's exact blinded source release is required")
    value = json.loads(args.release.read_text())
    if value.get("phase") != "original_v3_primary_released_for_new_range_validation":
        raise ValueError("Wrong original-primary release phase")
    if value.get("source_manifest_sha256") != SERIES_SHA:
        raise ValueError("Different original v3 series")
    for key in ("complete_original_primary_verified", "reservation_and_seat_coverage_verified", "terminal_coverage_verified"):
        if value.get(key) is not True:
            raise ValueError(f"Original-primary release does not establish {key}")
    files = value["files"]
    expected = ["control", "candidate", "candidate", "control", "candidate", "control", "control", "candidate"]
    if len(files) != 8:
        raise ValueError("All eight original v3 jobs are required")
    for i, item in enumerate(files):
        if item["job"] != f"{i:02d}_{expected[i]}" or item["arm"] != expected[i] or item["hands"] != 4000:
            raise ValueError("Wrong original v3 source job or quota")
    return files, None, value


def source_path(item, args):
    original = Path(item["path"])
    expected = "strategy_v2" if args.partition == "train" else "strategy_v3"
    if expected not in original.parts or "confirmation" not in original.parts:
        raise ValueError("Source path is outside the permitted original series")
    if args.report_root is None:
        result = original
    else:
        if original.parts.count("reports") != 1:
            raise ValueError("Unrecognized report relocation")
        result = args.report_root.joinpath(*original.parts[original.parts.index("reports") + 1:]).resolve()
        result.relative_to(args.report_root.resolve())
    if sha(result) != item["sha256"]:
        raise ValueError(f"Changed frozen log {item['job']}")
    return result


def selected_records(files, salt, args, coverage):
    """Read identities before card/action/label fields; never score other splits."""
    seat_maps = {}
    for item in files:
        path = source_path(item, args)
        terminals, reservations, seen_hands = 0, set(), set()
        events = {}
        positions = {}
        with path.open() as stream:
            for line in stream:
                record = json.loads(line)
                if args.partition != "train" and "hand_reserved" in record:
                    event = record["hand_reserved"]
                    index = event["hand_reservation_index"]
                    if index in events:
                        raise ValueError("Duplicate reservation event")
                    events[index] = event
                if "winnings_chips" not in record:
                    continue
                terminals += 1
                hand = record["hand"]
                if hand in seen_hands:
                    raise ValueError("Duplicate terminal hand")
                seen_hands.add(hand)
                if args.partition == "train":
                    selected = training_partition(salt, item["sha256"], hand)
                    identity = hand
                else:
                    identity = record["hand_reservation_index"]
                    if identity in reservations or identity not in events:
                        raise ValueError("Missing/duplicate/pre-event terminal reservation")
                    reservations.add(identity)
                    for key in ("game", "game_hand_attempt", "policy_seed_at_hand_start"):
                        if record[key] != events[identity][key]:
                            raise ValueError("Terminal reservation identity changed")
                    game, attempt = record["game"], record["game_hand_attempt"]
                    quota = item["hands"] // item["games"]
                    if not 0 <= game < item["games"] or not 0 <= attempt < quota or identity != game * quota + attempt:
                        raise ValueError("Wrong fixed game-block reservation")
                    key = f"poker-policy-hand-seed-v1:{item['seed']}:{identity}".encode("ascii")
                    seed = int.from_bytes(hashlib.sha256(key).digest()[:8], "big") or 1
                    if record["policy_seed_at_hand_start"] != seed:
                        raise ValueError("Wrong independent policy seed")
                    if record["client_pos"] not in (0, 1):
                        raise ValueError("Invalid original hero seat")
                    positions[identity] = record["client_pos"]
                    selected = holdout_partition(item["job"], identity) == args.partition
                if not selected:
                    continue
                if record.get("native_terminal_verified") is not True:
                    raise ValueError("Selected hand lacks the original native terminal check")
                coverage["selected_terminal_hands"] += 1
                coverage["selected_hands_by_arm"][item["arm"]] += 1
                yield item, record, identity
        if terminals != item["hands"] or sha(path) != item["sha256"]:
            raise ValueError("Incomplete or changed original log")
        if args.partition != "train":
            expected = set(range(item["hands"]))
            if reservations != expected or set(events) != expected:
                raise ValueError("Original planned reservation coverage is incomplete")
            counts = {str(p): sum(value == p for value in positions.values()) for p in (0, 1)}
            if counts != item["position_counts"]:
                raise ValueError("Original planned seat counts changed")
            if item["seed"] in seat_maps and positions != seat_maps[item["seed"]]:
                raise ValueError("Paired original jobs differ in reservation seats")
            seat_maps[item["seed"]] = positions


def run(args):
    check_plan(args.plan)
    if sha(args.replay) != REPLAY_SHA:
        raise ValueError("Changed frozen public replay dependency")
    files, salt, release = sources(args)
    expected_first = None
    if args.partition == "train":
        if args.diagnostic_rows is None or sha(args.diagnostic_rows) != DIAGNOSTIC_ROWS_SHA:
            raise ValueError("TRAIN requires the frozen diagnostic row identities")
        existing = [json.loads(line) for line in args.diagnostic_rows.read_text().splitlines()]
        expected_first = {(r["job"], r["hand"]): r for r in existing if r["river_decision_index"] == 0}
        for row in existing:
            first = expected_first[(row["job"], row["hand"])]
            if not first["solver_eligible"] and row["solver_eligible"]:
                raise ValueError("First-state and first-eligible cohorts are not equivalent")
    args.output.mkdir(parents=True, exist_ok=False)
    evaluation = load_module(args.replay, "root_range_frozen_replay")
    native = evaluation.Native(library=args.library)
    native.lib.poker_root_range_distribution.argtypes = [C.c_void_p, C.c_uint8, C.c_uint8,
        C.POINTER(C.c_double), C.POINTER(C.c_uint32), C.POINTER(C.c_double),
        C.POINTER(C.c_int), C.POINTER(C.c_double)]
    native.lib.poker_root_range_distribution.restype = C.c_int
    if native.lib.poker_root_range_distribution_pairs() != PAIRS:
        raise ValueError("Wrong distribution export ABI")
    library_hash = sha(args.library)
    coverage = {"selected_terminal_hands": 0, "selected_hands_by_arm": {"control": 0, "candidate": 0},
                "first_river_hands_before_scope": 0, "eligible_states": 0,
                "excluded_first_states": 0, "missing_or_illegal_targets": 0,
                "all_selected_target_reveals_present": True}
    entries, weights, ranks, shares, targets = [], [], [], [], []
    for item, hand, identity in selected_records(files, salt, args, coverage):
        river = [d for d in hand["decisions"] if d["street"] == 3]
        if not river:
            continue
        coverage["first_river_hands_before_scope"] += 1
        target_text = hand.get("slumbot_hole_cards")
        if (not isinstance(target_text, list) or len(target_text) != 2
            or not all(isinstance(c, str) and len(c) == 2 and c[0] in evaluation.RANKS and c[1] in evaluation.SUITS for c in target_text)
            or len(set(target_text)) != 2):
            raise ValueError("Missing/invalid selected river target; do not filter")
        target_cards = [evaluation.card(c) for c in target_text]
        first_eligible = None
        for river_index, decision in enumerate(river):
            state_input = {key: decision[key] for key in ("client_pos", "hole_cards", "board", "action")}
            state, players, board = evaluation.replay(native, state_input)
            hero = state_input["client_pos"]
            if len(state_input["board"]) != 5 or state[0] != hero or state[1] != 3:
                raise ValueError("Selected river state does not replay")
            if decision["pot_chips"] != state[2] or decision["call_chips"] != min(state[3] - players[hero][3], players[hero][0]):
                raise ValueError("Public decision money does not replay")
            if not hand["action"].startswith(state_input["action"]):
                raise ValueError("Decision is not an original public prefix")
            w, rank, share = (C.c_double * PAIRS)(), (C.c_uint32 * PAIRS)(), (C.c_double * PAIRS)()
            target, context = C.c_int(-1), (C.c_double * 3)()
            before = bytes(native.storage)
            accepted = native.lib.poker_root_range_distribution(native.storage, *target_cards, w, rank, share, C.byref(target), context)
            if not accepted or before != bytes(native.storage):
                raise ValueError("Invalid target/scope or mutating native export")
            if list(context)[2] != PAIRS:
                raise ValueError("Wrong legal holding count")
            eligible = bool(context[0]) and bool(context[1])
            if river_index:
                if not first_eligible and eligible:
                    raise ValueError("A later state would replace an excluded first state")
                continue
            first_eligible = eligible
            prefix_hash = hashlib.sha256(json.dumps(state_input, sort_keys=True).encode()).hexdigest()
            old = expected_first.get((item["job"], hand["hand"])) if expected_first is not None else None
            if expected_first is not None and (old is None or old["public_prefix_sha256"] != prefix_hash or old["solver_eligible"] != eligible):
                raise ValueError("TRAIN first-state identity or scope changed")
            if not eligible:
                coverage["excluded_first_states"] += 1
                continue
            wa, ra, sa = np.array(w, dtype=np.float64), np.array(rank, dtype=np.uint32), np.array(share, dtype=np.float64)
            ti = target.value
            if not np.isfinite(wa).all() or not (wa > 0).all() or not 0 <= ti < PAIRS:
                raise ValueError("Invalid positive range weights or target")
            if not np.isin(sa, [0., .5, 1.]).all() or not ((ra >> 24) <= 8).all():
                raise ValueError("Invalid showdown share or rank category")
            if old is not None:
                normalized = wa / wa.sum()
                if abs(normalized[ti] - old["target_pair_probability"]) > 2e-12 or abs(float((normalized * sa).sum()) - old["predicted_hero_share"]) > 2e-12 or sa[ti] != old["actual_hero_share"]:
                    raise ValueError("New exporter differs from frozen diagnostic")
            entries.append({"job": item["job"], "arm": item["arm"], "hand": hand["hand"],
                "identity": identity, "partition": args.partition, "public_prefix_sha256": prefix_hash,
                "call_chips": decision["call_chips"], "pot_chips": decision["pot_chips"], "hero_seat": hero})
            weights.append(wa); ranks.append(ra); shares.append(sa); targets.append(ti)
            coverage["eligible_states"] += 1
    if args.partition == "train" and (coverage["first_river_hands_before_scope"] != 2908 or coverage["eligible_states"] != 2907):
        raise ValueError("Unexpected fixed TRAIN cohort")
    if sha(args.library) != library_hash:
        raise ValueError("Native helper changed during export")
    order = sorted(range(len(entries)), key=lambda i: (entries[i]["job"], entries[i]["identity"]))
    if len({(entries[i]["job"], entries[i]["identity"]) for i in order}) != len(order):
        raise ValueError("Duplicate first-state hand identity")
    write_json(args.output / "rows.json", [entries[i] for i in order])
    np.savez_compressed(args.output / "distributions.npz", weights=np.array([weights[i] for i in order]),
        ranks=np.array([ranks[i] for i in order]), shares=np.array([shares[i] for i in order]),
        targets=np.array([targets[i] for i in order], dtype=np.int64))
    manifest = {"schema": 1, "partition": args.partition, "plan_sha256": PLAN_SHA,
        "exporter_sha256": sha(Path(__file__)), "native_library_sha256": library_hash,
        "replay_source_sha256": REPLAY_SHA, "sources": files, "coverage": coverage,
        "rows_sha256": sha(args.output / "rows.json"), "distributions_sha256": sha(args.output / "distributions.npz"),
        "model_sha256": sha(args.model) if args.model else None,
        "release_sha256": sha(args.release) if args.release else None,
        "choice_sha256": sha(args.choice) if args.choice else None,
        "forbidden_partition_labels_accessed": False, "terminal_winnings_used": False,
        "target_changes_predictions": False}
    write_json(args.output / "manifest.json", manifest)
    print(json.dumps({"partition": args.partition, "coverage": coverage, "manifest_sha256": sha(args.output / "manifest.json")}, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--partition", choices=("train", "val", "test"), required=True)
    p.add_argument("--plan", type=Path, required=True)
    p.add_argument("--split", type=Path)
    p.add_argument("--diagnostic-rows", type=Path)
    p.add_argument("--replay", type=Path, required=True)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--report-root", type=Path)
    p.add_argument("--release", type=Path)
    p.add_argument("--model", type=Path)
    p.add_argument("--choice", type=Path)
    p.add_argument("--output", type=Path, required=True)
    run(p.parse_args())


if __name__ == "__main__":
    main()
