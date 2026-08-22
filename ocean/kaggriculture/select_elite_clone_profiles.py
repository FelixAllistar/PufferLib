#!/usr/bin/env python3
"""Quality-gated, behavior-diverse shortlist from clone profile TSVs."""

from __future__ import annotations

import argparse
import csv
import math
import re
import statistics
from pathlib import Path


BEHAVIOR_METRICS = (
    "future_value_score", "mean_money", "gdp", "production_units",
    "crop_production_units", "animal_production_units", "successful_plants",
    "successful_animal_places", "sold_units", "sales_revenue",
    "bought_units", "purchase_spend", "crop_sold_units",
    "animal_product_sold_units", "strawberry_sold_units", "milk_sold_units",
    "ending_shed_units", "carrot_opportunity_response",
    "tomato_opportunity_response", "egg_opportunity_response",
    "water_coverage", "neglect_deaths", "planting_day_deaths",
    "plants_alive", "animals_alive", "land_purchases",
    "productive_extra_tiles", "orders_per_turn", "hire_orders",
)

QUALITY_WEIGHTS = {
    "future_value_score": 0.15,
    "mean_money": 0.20,
    "gdp": 0.20,
    "production_units": 0.10,
    "successful_plants": 0.075,
    "successful_animal_places": 0.075,
    "sales_revenue": 0.15,
    "water_coverage": 0.05,
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def number(row: dict[str, str], key: str) -> float:
    try:
        value = float(row.get(key, "0") or 0)
        return value if math.isfinite(value) else 0.0
    except ValueError:
        return 0.0


def percentile_ranks(values: dict[str, float]) -> dict[str, float]:
    ordered = sorted(values.items(), key=lambda item: (item[1], item[0]))
    denominator = max(1, len(ordered) - 1)
    return {policy: index / denominator
            for index, (policy, _) in enumerate(ordered)}


def profile_name(path: Path) -> str:
    name = path.stem.lower()
    for opponent in ("pass", "rules"):
        if (f"stoch_{opponent}" in name
                or f"stochastic_{opponent}" in name):
            return f"stoch_{opponent}"
        if (f"det_{opponent}" in name
                or f"deterministic_{opponent}" in name):
            return f"det_{opponent}"
    raise ValueError(f"profile filename must contain mode/opponent: {path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("profiles", nargs=4, type=Path)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--count", type=int, default=12)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    profiles: dict[str, dict[str, dict[str, str]]] = {}
    policies: set[str] | None = None
    for path in args.profiles:
        label = profile_name(path)
        rows = {row["policy"]: row for row in read_tsv(path)}
        profiles[label] = rows
        policies = set(rows) if policies is None else policies & set(rows)
    if set(profiles) != {"stoch_pass", "stoch_rules", "det_pass", "det_rules"}:
        raise SystemExit("need exactly stoch/det x pass/rules profiles")
    if not policies:
        raise SystemExit("profiles have no common policies")

    plan_by_dataset = {
        row["dataset_id"]: row for row in read_tsv(args.plan)
    }
    dataset_pattern = re.compile(r"_([0-9a-f]{12})_[0-9]+x[0-9]+_e[0-9]+$")

    quality = {policy: 0.0 for policy in policies}
    mode_weights = {
        "stoch_pass": 0.35, "stoch_rules": 0.35,
        "det_pass": 0.15, "det_rules": 0.15,
    }
    for profile, rows in profiles.items():
        for metric, metric_weight in QUALITY_WEIGHTS.items():
            ranks = percentile_ranks(
                {policy: number(rows[policy], metric) for policy in policies})
            for policy in policies:
                quality[policy] += mode_weights[profile] * metric_weight * ranks[policy]

    active: dict[str, bool] = {}
    for policy in policies:
        stochastic = [profiles[name][policy]
                      for name in ("stoch_pass", "stoch_rules")]
        active[policy] = any(
            number(row, "gdp") >= 1000
            or number(row, "production_units") >= 10
            or number(row, "sales_revenue") >= 1000
            or number(row, "successful_plants")
                + number(row, "successful_animal_places") >= 10
            for row in stochastic
        )

    feature_keys = [(profile, metric) for profile in sorted(profiles)
                    for metric in BEHAVIOR_METRICS]
    transformed: dict[tuple[str, str], dict[str, float]] = {}
    for profile, metric in feature_keys:
        raw = {}
        for policy in policies:
            value = number(profiles[profile][policy], metric)
            raw[policy] = value if metric in {
                "water_coverage", "orders_per_turn"
            } else math.log1p(max(0.0, value))
        values = list(raw.values())
        mean = statistics.fmean(values)
        std = statistics.pstdev(values)
        if std > 1e-9:
            transformed[(profile, metric)] = {
                policy: (value - mean) / std for policy, value in raw.items()
            }

    vectors = {
        policy: [column[policy] for column in transformed.values()]
        for policy in policies
    }

    def distance(left: str, right: str) -> float:
        a, b = vectors[left], vectors[right]
        if not a:
            return 0.0
        return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a))

    candidates = [policy for policy in policies if active[policy]]
    if len(candidates) < args.count:
        candidates = sorted(policies, key=quality.get, reverse=True)
    selected = [max(candidates, key=lambda policy: (quality[policy], policy))]
    selected_distance = {selected[0]: 0.0}
    while len(selected) < min(args.count, len(candidates)):
        choice = None
        choice_score = -1.0
        choice_distance = 0.0
        for policy in candidates:
            if policy in selected:
                continue
            minimum = min(distance(policy, prior) for prior in selected)
            score = minimum + 0.35 * quality[policy]
            if score > choice_score:
                choice, choice_score, choice_distance = policy, score, minimum
        assert choice is not None
        selected.append(choice)
        selected_distance[choice] = choice_distance

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "rank", "policy", "agent", "dataset_id", "active", "quality",
        "diversity_distance", "stoch_pass_future", "stoch_pass_money",
        "stoch_pass_gdp", "stoch_rules_future", "stoch_rules_money",
        "stoch_rules_gdp", "det_pass_future", "det_pass_money",
        "det_pass_gdp", "det_rules_future", "det_rules_money",
        "det_rules_gdp",
    ]
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fields, delimiter="\t")
        writer.writeheader()
        for rank, policy in enumerate(selected, 1):
            match = dataset_pattern.search(policy)
            dataset_id = match.group(1) if match else ""
            plan = plan_by_dataset.get(dataset_id, {})
            output = {
                "rank": rank, "policy": policy,
                "agent": plan.get("agent", policy), "dataset_id": dataset_id,
                "active": int(active[policy]),
                "quality": f"{quality[policy]:.6f}",
                "diversity_distance": f"{selected_distance[policy]:.6f}",
            }
            for profile in profiles:
                output[f"{profile}_future"] = number(
                    profiles[profile][policy], "future_value_score")
                output[f"{profile}_money"] = number(
                    profiles[profile][policy], "mean_money")
                output[f"{profile}_gdp"] = number(
                    profiles[profile][policy], "gdp")
            writer.writerow(output)
    print(f"selected {len(selected)} of {len(policies)} policies -> {args.output}")


if __name__ == "__main__":
    main()
