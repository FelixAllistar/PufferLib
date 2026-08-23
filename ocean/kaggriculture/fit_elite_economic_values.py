#!/usr/bin/env python3
"""Estimate positive economic asset values from elite Kaggriculture replays.

This deliberately does not regress final money directly on every observation.
Doing so assigns value to time, player identity, and future actions that have
not happened yet.  Instead it follows each planted crop and placed animal,
measures realized harvests, and estimates the conversion rates between bought
capital, productive assets, and products that elite players actually sell.

The output is both an auditable JSON report and an INI fragment consumed by the
nonnegative high-water reward.  All fitted values are nonnegative and remain
ordinary INI knobs, so subsequent sweeps can override them.
"""

from __future__ import annotations

import argparse
import collections
import json
import math
import pathlib
import statistics
import sys
from dataclasses import dataclass
from typing import Any, Iterable

from import_elite_replays import (
    EXPECTED_STEPS,
    _expand_inputs,
    _iter_replays,
    _validate_episode,
    _version_tuple,
)


CROPS = ("WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON")
ANIMALS = ("GOOSE", "COW", "SHEEP")
PRODUCTS = (*CROPS, "EGG", "MILK", "WOOL", "FERTILIZER")
ANIMAL_PRODUCT = {"GOOSE": "EGG", "COW": "MILK", "SHEEP": "WOOL"}

# The current 1.32.7 rules.  These are used only to normalize observed output
# by the number of production opportunities; the fitted output still comes
# from replay behavior rather than the theoretical maximum.
CROP_RULES = {
    "WHEAT": dict(first=2, interval=0, events=1, max_units=6, ongoing=False),
    "CARROT": dict(first=2, interval=0, events=1, max_units=4, ongoing=False),
    "TOMATO": dict(first=8, interval=1, events=4, max_units=4, ongoing=True),
    "STRAWBERRY": dict(first=10, interval=2, events=4, max_units=4, ongoing=True),
    "MELON": dict(first=10, interval=0, events=1, max_units=6, ongoing=False),
}
ANIMAL_RULES = {
    "GOOSE": dict(first=4, interval=1),
    "COW": dict(first=8, interval=2),
    "SHEEP": dict(first=6, interval=3),
}


@dataclass
class Asset:
    kind: str
    name: str
    placed_day: int
    realized_units: float = 0.0
    observations: int = 0
    maintained: int = 0
    at_risk: int = 0


def _agent_name(episode: dict[str, Any], player: int) -> str:
    agents = episode.get("info", {}).get("Agents", [])
    if player < len(agents) and isinstance(agents[player], dict):
        return str(agents[player].get("Name", ""))
    return ""


def _tile(farm: dict[str, Any], position: Any) -> dict[str, Any] | None:
    if not isinstance(position, (list, tuple)) or len(position) != 2:
        return None
    x, y = int(position[0]), int(position[1])
    tiles = farm.get("tiles", [])
    if y < 0 or y >= len(tiles) or x < 0 or x >= len(tiles[y]):
        return None
    value = tiles[y][x]
    return value if isinstance(value, dict) else None


def _scan_assets(
    observation: dict[str, Any], player: int, assets: dict[tuple[Any, ...], Asset]
) -> None:
    farm = observation["farms"][player]
    for y, row in enumerate(farm.get("tiles", [])):
        for x, tile in enumerate(row):
            if not isinstance(tile, dict):
                continue
            kind = str(tile.get("kind", "")).upper()
            if kind == "PLANT" and str(tile.get("crop", "")).upper() in CROPS:
                name = str(tile["crop"]).upper()
                placed = int(tile.get("planted_day", observation.get("day", 0)))
                key = ("crop", x, y, name, placed)
                asset = assets.setdefault(key, Asset("crop", name, placed))
                asset.observations += 1
                asset.maintained += bool(tile.get("watered_today", False))
                asset.at_risk += int(tile.get("consecutive_unwatered", 0)) > 0
            elif kind in {"COOP", "PASTURE"}:
                name = str(tile.get("animal", "")).upper()
                if name not in ANIMALS:
                    continue
                placed = int(tile.get("placed_day", observation.get("day", 0)))
                key = ("animal", x, y, name, placed)
                asset = assets.setdefault(key, Asset("animal", name, placed))
                asset.observations += 1
                asset.maintained += bool(tile.get("fed_today", False))
                asset.at_risk += int(tile.get("consecutive_unfed", 0)) > 0


def _harvests(
    observation: dict[str, Any], action: dict[str, Any], player: int,
    assets: dict[tuple[Any, ...], Asset], produced: collections.Counter[str],
) -> None:
    farm = observation["farms"][player]
    positions = [farm.get("farmer"), *farm.get("hands", [])]
    commands = [action.get("farmer", ["PASS"]), *action.get("hands", [])]
    if len(commands) < len(positions):
        commands.extend([["PASS"]] * (len(positions) - len(commands)))
    harvested_keys: set[tuple[Any, ...]] = set()
    for position, command in zip(positions, commands):
        if not isinstance(command, (list, tuple)) or not command:
            continue
        if str(command[0]).upper() != "HARVEST":
            continue
        tile = _tile(farm, position)
        if tile is None:
            continue
        units = max(0, int(tile.get("yield_units", 0)))
        if units == 0:
            continue
        x, y = int(position[0]), int(position[1])
        if str(tile.get("kind", "")).upper() == "PLANT":
            name = str(tile.get("crop", "")).upper()
            key = ("crop", x, y, name, int(tile.get("planted_day", 0)))
            product = name
        else:
            name = str(tile.get("animal", "")).upper()
            key = ("animal", x, y, name, int(tile.get("placed_day", 0)))
            product = ANIMAL_PRODUCT.get(name, "")
        asset = assets.get(key)
        # Several hands may demonstrate HARVEST on one tile in the same turn,
        # but the simulator transfers its yield only once.
        if asset is not None and product and key not in harvested_keys:
            asset.realized_units += units
            produced[product] += units
            harvested_keys.add(key)


def _market_counts(
    observation: dict[str, Any], action: dict[str, Any], player: int,
    seed_buys: collections.Counter[str], animal_buys: collections.Counter[str],
    product_buys: collections.Counter[str], sold: collections.Counter[str],
) -> None:
    shed = dict(observation.get("private", {}).get("shed", {}))
    for order in action.get("market", []):
        if not isinstance(order, (list, tuple)) or not order:
            continue
        command = str(order[0]).upper()
        item = str(order[1]).upper() if len(order) > 1 else ""
        try:
            quantity = max(0, int(order[2])) if len(order) > 2 else 1
        except (TypeError, ValueError):
            quantity = 1
        if command == "BUY_SEED" and item in CROPS:
            seed_buys[item] += quantity
        elif command == "BUY_PRODUCT" and item in PRODUCTS:
            product_buys[item] += quantity
        elif command == "BUY_ANIMAL" and item in ANIMALS:
            animal_buys[item] += quantity
        elif command == "SELL" and item in PRODUCTS:
            # SELL draws from the shed.  Capping by the observed inventory
            # prevents oversized requested quantities from inflating the fit.
            filled = min(quantity, max(0, int(shed.get(item, 0))))
            sold[item] += filled
            shed[item] = max(0, int(shed.get(item, 0)) - filled)


def _possible_animal_events(name: str, placed_day: int) -> int:
    rule = ANIMAL_RULES[name]
    first = placed_day + int(rule["first"])
    if first > 29:
        return 0
    return 1 + (29 - first) // int(rule["interval"])


def _smoothed_mean(
    total: float, count: float, prior: float, prior_weight: float
) -> float:
    return (total + prior * prior_weight) / (count + prior_weight)


def _smoothed_rate(
    successes: float, attempts: float, prior: float, prior_weight: float
) -> float:
    return min(1.0, max(0.0, _smoothed_mean(
        successes, attempts, prior, prior_weight
    )))


def _round(value: float) -> float:
    return float(f"{value:.6f}")


def _write_ini(path: pathlib.Path, fit: dict[str, Any]) -> None:
    lines = [
        "# Generated by fit_elite_economic_values.py.",
        "# Discount-consistent future-cash potential. Noncash value tapers to",
        "# zero before done, so the dense return still targets terminal cash.",
        "# These are defaults, not constants: keep them in [env] and sweep.",
        "reward_progress_scale = 1.0",
        "reward_progress_terminal_money_scale = 0.0",
        "reward_progress_win_scale = 1.0",
        "reward_progress_liquidation_days = 6.0",
        "reward_progress_seed_scale = 1.0",
        "reward_progress_crop_scale = 1.0",
        "reward_progress_animal_scale = 1.0",
        "reward_progress_product_scale = 1.0",
        "reward_progress_maintenance_scale = 0.0",
        "reward_progress_land_scale = 1.0",
        f"reward_progress_health_ratio = {fit['health_ratio']:.6f}",
    ]
    for crop in CROPS:
        key = crop.lower()
        lines.append(
            f"reward_progress_crop_{key}_units = "
            f"{fit['crop_expected_units'][crop]:.6f}"
        )
        lines.append(
            f"reward_progress_seed_{key}_realization = "
            "1.000000"
        )
    for animal in ANIMALS:
        key = animal.lower()
        lines.append(
            f"reward_progress_animal_{key}_units_per_event = "
            f"{fit['animal_units_per_event'][animal]:.6f}"
        )
        lines.append(
            f"reward_progress_animal_{key}_realization = "
            "1.000000"
        )
    for product in PRODUCTS:
        lines.append(
            f"reward_progress_product_{product.lower()}_realization = "
            f"{fit['product_realization'][product]:.6f}"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_sweep(path: pathlib.Path) -> None:
    # Keep empirical per-item values fixed initially. These aggregate dials
    # provide useful ablations without a confounded 25-dimensional search.
    ranges = {
        "reward_progress_scale": (0.0, 2.0),
        "reward_progress_terminal_money_scale": (0.0, 2.0),
        "reward_progress_win_scale": (0.0, 2.0),
        "reward_progress_liquidation_days": (0.0, 10.0),
        "reward_progress_seed_scale": (0.0, 1.5),
        "reward_progress_crop_scale": (0.0, 2.0),
        "reward_progress_animal_scale": (0.0, 2.0),
        "reward_progress_product_scale": (0.0, 1.5),
        "reward_progress_maintenance_scale": (0.0, 2.0),
        "reward_progress_land_scale": (0.0, 1.25),
        "reward_progress_health_ratio": (0.0, 1.0),
    }
    lines = [
        "# Generated compact sweep ranges. Put desired keys in sweep_only.",
        "# Zero remains valid where it is a meaningful hard ablation.",
    ]
    for key, (minimum, maximum) in ranges.items():
        lines.extend([
            "",
            f"[sweep.env.{key}]",
            "distribution = uniform",
            f"min = {minimum}",
            f"max = {maximum}",
            "scale = auto",
        ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", help="replay JSON/ZIP/GZ paths")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--ini-output", type=pathlib.Path)
    parser.add_argument("--sweep-output", type=pathlib.Path)
    parser.add_argument("--minimum-version", default="1.32.7")
    parser.add_argument("--exact-version", default="1.32.7")
    parser.add_argument("--minimum-final-money", type=float, default=50_000.0)
    parser.add_argument("--winner-only", action="store_true")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    paths = _expand_inputs(args.inputs)
    minimum_version = _version_tuple(args.minimum_version)
    exact_version = _version_tuple(args.exact_version)
    accepted = 0
    skipped: collections.Counter[str] = collections.Counter()
    agents: collections.Counter[str] = collections.Counter()
    seed_buys: collections.Counter[str] = collections.Counter()
    animal_buys: collections.Counter[str] = collections.Counter()
    product_buys: collections.Counter[str] = collections.Counter()
    sold: collections.Counter[str] = collections.Counter()
    produced: collections.Counter[str] = collections.Counter()
    crop_assets: dict[str, list[Asset]] = {name: [] for name in CROPS}
    animal_assets: dict[str, list[Asset]] = {name: [] for name in ANIMALS}

    for _source, episode in _iter_replays(paths):
        reason = _validate_episode(
            episode, minimum_version, EXPECTED_STEPS, exact_version
        )
        if reason is not None:
            skipped[reason] += 1
            continue
        rewards = [float(value) for value in episode["rewards"]]
        best = max(rewards)
        chosen = [
            p for p in range(2)
            if rewards[p] >= args.minimum_final_money
            and (not args.winner_only or rewards[p] == best)
        ]
        if not chosen:
            skipped["money_or_winner_filter"] += 1
            continue
        for player in chosen:
            assets: dict[tuple[Any, ...], Asset] = {}
            for turn in range(EXPECTED_STEPS):
                observation = episode["steps"][turn][player]["observation"]
                _scan_assets(observation, player, assets)
                if turn + 1 >= EXPECTED_STEPS:
                    continue
                action = episode["steps"][turn + 1][player]["action"]
                _harvests(observation, action, player, assets, produced)
                _market_counts(
                    observation, action, player, seed_buys, animal_buys,
                    product_buys, sold,
                )
            for asset in assets.values():
                target = crop_assets if asset.kind == "crop" else animal_assets
                target[asset.name].append(asset)
            agents[_agent_name(episode, player)] += 1
            accepted += 1
        if args.limit and accepted >= args.limit:
            break

    if accepted == 0:
        raise SystemExit("no compatible elite player trajectories accepted")

    crop_expected: dict[str, float] = {}
    for name, values in crop_assets.items():
        # Include failures: they are part of elite empirical realizability.
        # A small rules-derived prior prevents a rare opportunity crop from
        # becoming permanently worthless when the sample scarcely chose it.
        crop_expected[name] = _round(_smoothed_mean(
            sum(asset.realized_units for asset in values), len(values),
            float(CROP_RULES[name]["max_units"]), 8.0,
        ))

    animal_units_per_event: dict[str, float] = {}
    for name, values in animal_assets.items():
        events = sum(_possible_animal_events(name, a.placed_day) for a in values)
        units = sum(a.realized_units for a in values)
        animal_units_per_event[name] = _round(_smoothed_mean(
            units, events, 1.0, 20.0
        ))

    seed_realization = {
        name: _round(_smoothed_rate(
            len(crop_assets[name]), seed_buys[name], 0.5, 20.0
        ))
        for name in CROPS
    }
    animal_realization = {
        name: _round(_smoothed_rate(
            len(animal_assets[name]), animal_buys[name], 0.5, 20.0
        ))
        for name in ANIMALS
    }
    product_realization = {
        name: _round(min(0.98, max(0.20, _smoothed_rate(
            sold[name], produced[name] + product_buys[name], 0.70, 50.0
        ))))
        for name in PRODUCTS
    }

    observations = sum(
        asset.observations
        for values in (*crop_assets.values(), *animal_assets.values())
        for asset in values
    )
    maintained = sum(
        asset.maintained
        for values in (*crop_assets.values(), *animal_assets.values())
        for asset in values
    )
    at_risk = sum(
        asset.at_risk
        for values in (*crop_assets.values(), *animal_assets.values())
        for asset in values
    )
    # Estimate residual output for assets that visibly entered an at-risk
    # state relative to same-type assets that never did. This is more direct
    # than turning maintenance coverage itself into an arbitrary discount.
    coverage = maintained / observations if observations else 0.5
    health_ratios: list[tuple[float, int]] = []
    for groups in (crop_assets, animal_assets):
        for values in groups.values():
            healthy = [a.realized_units for a in values if a.at_risk == 0]
            risky = [a.realized_units for a in values if a.at_risk > 0]
            if len(healthy) < 8 or len(risky) < 8:
                continue
            healthy_mean = statistics.fmean(healthy)
            if healthy_mean <= 0:
                continue
            ratio = min(1.0, max(0.0, statistics.fmean(risky) / healthy_mean))
            health_ratios.append((ratio, min(len(healthy), len(risky))))
    if health_ratios:
        health_ratio = sum(r * n for r, n in health_ratios) / sum(
            n for _, n in health_ratios
        )
    else:
        health_ratio = 0.5
    health_ratio = _round(min(0.90, max(0.10, health_ratio)))

    fit: dict[str, Any] = {
        "schema_version": 1,
        "input_archives": [path.name for path in paths],
        "accepted_player_trajectories": accepted,
        "distinct_agents": len([name for name in agents if name]),
        "agent_trajectory_counts": dict(agents.most_common()),
        "minimum_version": args.minimum_version,
        "exact_version": args.exact_version,
        "minimum_final_money": args.minimum_final_money,
        "winner_only": args.winner_only,
        "skipped": dict(skipped),
        "crop_assets": {name: len(values) for name, values in crop_assets.items()},
        "animal_assets": {name: len(values) for name, values in animal_assets.items()},
        "seed_buys": dict(seed_buys),
        "animal_buys": dict(animal_buys),
        "product_buys": dict(product_buys),
        "produced_harvest_units": dict(produced),
        "requested_filled_sell_units": dict(sold),
        "crop_expected_units": crop_expected,
        "animal_units_per_event": animal_units_per_event,
        "seed_realization": seed_realization,
        "animal_realization": animal_realization,
        "product_realization": product_realization,
        "maintenance_coverage": _round(coverage),
        "at_risk_fraction": _round(at_risk / observations if observations else 0),
        "health_ratio": health_ratio,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(fit, indent=2, sort_keys=True) + "\n")
    ini_output = args.ini_output or args.output.with_suffix(".ini")
    _write_ini(ini_output, fit)
    sweep_output = args.sweep_output or args.output.with_suffix(".sweep.ini")
    _write_sweep(sweep_output)
    print(json.dumps({
        "accepted_player_trajectories": accepted,
        "distinct_agents": fit["distinct_agents"],
        "crop_expected_units": crop_expected,
        "animal_units_per_event": animal_units_per_event,
        "output": str(args.output),
        "ini_output": str(ini_output),
        "sweep_output": str(sweep_output),
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
