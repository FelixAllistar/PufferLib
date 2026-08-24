#!/usr/bin/env python3
"""Select balanced, complete replay states for a reset curriculum stage."""

from __future__ import annotations

import argparse
import collections
import csv
import itertools
import json
import pathlib
import random
from typing import Iterable


STAGES = {
    "sell": ("sell_now",),
    "market": (
        "sell_now", "hold_for_later", "buy_opportunity",
        "carrot_opportunity", "tomato_opportunity", "egg_opportunity",
        "liquidation_1d", "liquidation_3d",
    ),
    "maintenance": ("maintenance_profitable", "harvest_ready", "recovery"),
    "investment": ("short_investment", "medium_investment", "early_expansion"),
    "full": (
        "sell_now", "hold_for_later", "buy_opportunity",
        "carrot_opportunity", "tomato_opportunity", "egg_opportunity",
        "liquidation_1d", "liquidation_3d", "liquidation_6d",
        "maintenance_profitable", "maintenance_unprofitable", "harvest_ready",
        "short_investment", "medium_investment", "early_expansion", "recovery",
    ),
}


def state_key(row: dict[str, str]) -> tuple[str, str, int]:
    return row["source"], row["episode_id"], int(row["turn"])


def grouped_rows(path: pathlib.Path) -> Iterable[tuple[tuple[str, str, int], list[dict[str, str]]]]:
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        for key, rows in itertools.groupby(reader, key=state_key):
            yield key, [dict(row) for row in rows]


def reservoir_add(
    reservoir: list[tuple[str, str, int]], key: tuple[str, str, int],
    seen: int, capacity: int, rng: random.Random,
) -> None:
    if len(reservoir) < capacity:
        reservoir.append(key)
        return
    replacement = rng.randrange(seen)
    if replacement < capacity:
        reservoir[replacement] = key


def select(args: argparse.Namespace) -> dict[str, object]:
    index = pathlib.Path(args.index)
    output = pathlib.Path(args.output)
    scenarios = tuple(args.scenarios.split(",")) if args.scenarios else STAGES[args.stage]
    scenarios = tuple(value.strip() for value in scenarios if value.strip())
    if not scenarios:
        raise ValueError("at least one scenario is required")

    rng = random.Random(args.seed)
    reservoirs: dict[str, list[tuple[str, str, int]]] = {name: [] for name in scenarios}
    seen = collections.Counter()
    eligible_states = 0
    for key, rows in grouped_rows(index):
        final_money = max(float(row["final_money"]) for row in rows)
        if final_money < args.min_final_money:
            continue
        if args.winner_only and not any(int(row["winner"]) for row in rows):
            continue
        tags = {
            tag
            for row in rows
            for tag in row["scenarios"].split(",")
            if tag
        }
        matched = [name for name in scenarios if name in tags]
        if not matched:
            continue
        eligible_states += 1
        for name in matched:
            seen[name] += 1
            reservoir_add(
                reservoirs[name], key, seen[name],
                args.max_states_per_scenario, rng,
            )

    chosen = {key for values in reservoirs.values() for key in values}
    output.parent.mkdir(parents=True, exist_ok=True)
    written_rows = 0
    written_states: set[tuple[str, str, int]] = set()
    with index.open(encoding="utf-8", newline="") as src, output.open(
        "w", encoding="utf-8", newline=""
    ) as dst:
        reader = csv.DictReader(src, delimiter="\t")
        if reader.fieldnames is None:
            raise ValueError(f"missing header: {index}")
        writer = csv.DictWriter(dst, fieldnames=reader.fieldnames, delimiter="\t")
        writer.writeheader()
        for row in reader:
            key = state_key(row)
            if key in chosen:
                writer.writerow(row)
                written_rows += 1
                written_states.add(key)

    if written_states != chosen:
        raise RuntimeError(f"failed to emit {len(chosen - written_states)} chosen states")
    summary = {
        "format": "kaggriculture_replay_state_stage_v1",
        "index": str(index),
        "output": str(output),
        "stage": args.stage,
        "scenarios": scenarios,
        "seed": args.seed,
        "min_final_money": args.min_final_money,
        "winner_only": args.winner_only,
        "max_states_per_scenario": args.max_states_per_scenario,
        "eligible_states": eligible_states,
        "seen_per_scenario": dict(seen),
        "sampled_per_scenario": {
            name: len(values) for name, values in reservoirs.items()
        },
        "selected_states": len(chosen),
        "written_rows": written_rows,
    }
    pathlib.Path(f"{output}.summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--stage", choices=tuple(STAGES), default="sell")
    parser.add_argument("--scenarios", help="Comma-separated override for --stage")
    parser.add_argument("--max-states-per-scenario", type=int, default=5000)
    parser.add_argument("--min-final-money", type=float, default=0)
    parser.add_argument("--winner-only", action="store_true")
    parser.add_argument("--seed", type=int, default=707)
    args = parser.parse_args(argv)
    if args.max_states_per_scenario < 1:
        parser.error("--max-states-per-scenario must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    print(json.dumps(select(parse_args(argv)), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
