#!/usr/bin/env python3
"""Stream official Kaggriculture replays into a curriculum-state index.

This is deliberately a read-only S0a tool. It records where useful states live
and classifies their observable economics, but does not claim that an encoded
observation is a resumable simulator state. S0b will reconstruct and verify a
complete native KGState by replaying both action streams through the core.
"""

from __future__ import annotations

import argparse
import collections
import csv
import glob
import gzip
import io
import json
import pathlib
import sys
import zipfile
from typing import Any, Iterable, Iterator


PRODUCT_BASE_PRICE = {
    "WHEAT": 25,
    "CARROT": 35,
    "TOMATO": 60,
    "STRAWBERRY": 120,
    "MELON": 250,
    "EGG": 50,
    "MILK": 160,
    "WOOL": 200,
    "FERTILIZER": 100,
}
OPPORTUNITY_PRODUCTS = ("CARROT", "TOMATO", "EGG")
MAINTENANCE_OPS = {"WATER", "FEED", "CARE"}
HARVEST_OPS = {"HARVEST", "COLLECT_FERTILIZER"}
INVESTMENT_MARKET_OPS = {"BUY_SEED", "BUY_ANIMAL", "BUY_LAND", "HIRE"}

FIELDS = (
    "state_key", "episode_id", "source", "module_version", "seed", "player",
    "agent", "final_money", "winner", "turn", "day", "hour",
    "remaining_turns", "remaining_days", "money", "product_units",
    "seed_units", "plants", "animals", "weeds", "maintenance_due",
    "unlocked_tiles", "carrot_price_ratio", "tomato_price_ratio",
    "egg_price_ratio", "expert_unit_ops", "expert_market_ops", "scenarios",
)


def version_tuple(value: Any) -> tuple[int, ...]:
    parts: list[int] = []
    for token in str(value).split("."):
        digits = "".join(char for char in token if char.isdigit())
        if not digits:
            break
        parts.append(int(digits))
    return tuple(parts)


def expand_inputs(values: Iterable[str]) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for value in values:
        matches = glob.glob(value, recursive=True) or [value]
        for match in matches:
            path = pathlib.Path(match)
            candidates = (
                sorted(child for child in path.rglob("*") if child.is_file())
                if path.is_dir() else [path]
            )
            for candidate in candidates:
                if candidate.suffix.lower() not in {".json", ".zip", ".gz"}:
                    continue
                resolved = candidate.resolve()
                if resolved not in seen:
                    seen.add(resolved)
                    paths.append(candidate)
    return paths


def iter_replays(paths: Iterable[pathlib.Path]) -> Iterator[tuple[str, dict[str, Any]]]:
    for path in paths:
        suffix = path.suffix.lower()
        if suffix == ".zip":
            with zipfile.ZipFile(path) as archive:
                for name in sorted(archive.namelist()):
                    if name.lower().endswith(".json") and not name.endswith("/"):
                        with archive.open(name) as raw:
                            with io.TextIOWrapper(raw, encoding="utf-8") as stream:
                                yield f"{path}:{name}", json.load(stream)
        elif suffix == ".gz":
            with gzip.open(path, "rt", encoding="utf-8") as stream:
                yield str(path), json.load(stream)
        elif suffix == ".json":
            with path.open(encoding="utf-8") as stream:
                yield str(path), json.load(stream)


def validate_episode(episode: dict[str, Any], minimum_version: tuple[int, ...]) -> str | None:
    if episode.get("name") != "kaggriculture":
        return "wrong_environment"
    if version_tuple(episode.get("module_version", "0")) < minimum_version:
        return "old_module_version"
    steps = episode.get("steps")
    if not isinstance(steps, list) or len(steps) < 2:
        return "missing_steps"
    if episode.get("statuses") != ["DONE", "DONE"]:
        return "not_done"
    rewards = episode.get("rewards")
    if not isinstance(rewards, list) or len(rewards) != 2:
        return "missing_rewards"
    for step in steps:
        if not isinstance(step, list) or len(step) != 2:
            return "bad_step_shape"
        for record in step:
            if not isinstance(record, dict) or not isinstance(record.get("observation"), dict):
                return "missing_observation"
    return None


def agent_names(episode: dict[str, Any]) -> list[str]:
    info = episode.get("info") or {}
    names = info.get("TeamNames")
    if isinstance(names, list) and len(names) == 2:
        return [str(value) for value in names]
    agents = info.get("Agents")
    if isinstance(agents, list) and len(agents) == 2:
        return [str((value or {}).get("Name", f"player_{idx}")) for idx, value in enumerate(agents)]
    return ["player_0", "player_1"]


def action_ops(action: dict[str, Any]) -> tuple[list[str], list[str]]:
    unit_ops: list[str] = []
    for value in [action.get("farmer"), *(action.get("hands") or [])]:
        if isinstance(value, (list, tuple)) and value:
            unit_ops.append(str(value[0]).upper())
    market_ops = [
        str(value[0]).upper()
        for value in action.get("market", [])
        if isinstance(value, (list, tuple)) and value
    ]
    return unit_ops, market_ops


def tile_counts(observation: dict[str, Any], player: int) -> dict[str, int]:
    counts = collections.Counter()
    farms = observation.get("farms") or []
    if player >= len(farms) or not isinstance(farms[player], dict):
        return dict(counts)
    for row in farms[player].get("tiles", []):
        for tile in row:
            if tile == "LOCKED":
                continue
            counts["unlocked_tiles"] += 1
            if not isinstance(tile, dict):
                continue
            kind = str(tile.get("kind", "")).upper()
            if kind == "PLANT":
                counts["plants"] += 1
                if not bool(tile.get("watered_today", False)):
                    counts["maintenance_due"] += 1
                if int(tile.get("consecutive_unwatered", 0)) > 0:
                    counts["neglected"] += 1
            elif kind in {"COOP", "PASTURE"} and tile.get("animal"):
                counts["animals"] += 1
                if not bool(tile.get("fed_today", False)):
                    counts["maintenance_due"] += 1
                if kind == "PASTURE" and not bool(tile.get("cared_today", False)):
                    counts["maintenance_due"] += 1
                if int(tile.get("consecutive_unfed", 0)) > 0:
                    counts["neglected"] += 1
            elif kind == "WEED":
                counts["weeds"] += 1
    return dict(counts)


def inventory_counts(observation: dict[str, Any]) -> tuple[int, int]:
    private = observation.get("private") or {}
    shed = private.get("shed") or {}
    product_units = sum(max(0, int(shed.get(product, 0))) for product in PRODUCT_BASE_PRICE)
    for inventory in private.get("inventories") or []:
        if isinstance(inventory, dict):
            product_units += sum(max(0, int(inventory.get(product, 0))) for product in PRODUCT_BASE_PRICE)
    seeds = private.get("seeds") or {}
    return product_units, sum(max(0, int(value)) for value in seeds.values())


def classify_state(
    observation: dict[str, Any], action: dict[str, Any], player: int,
    remaining_turns: int, turns_per_day: int,
) -> tuple[dict[str, Any], list[str], list[str], list[str]]:
    unit_ops, market_ops = action_ops(action)
    tiles = tile_counts(observation, player)
    product_units, seed_units = inventory_counts(observation)
    prices = (observation.get("market") or {}).get("prices") or {}
    ratios = {
        product: float(prices.get(product, PRODUCT_BASE_PRICE[product])) / PRODUCT_BASE_PRICE[product]
        for product in OPPORTUNITY_PRODUCTS
    }
    remaining_days = remaining_turns / max(1, turns_per_day)
    scenarios: set[str] = set()

    if "SELL" in market_ops:
        scenarios.add("sell_now")
    elif product_units > 0:
        scenarios.add("hold_for_later")
    if any(op.startswith("BUY_") for op in market_ops):
        scenarios.add("buy_opportunity")
    for product, ratio in ratios.items():
        if ratio >= 1.25:
            scenarios.add(f"{product.lower()}_opportunity")
    if remaining_days <= 1:
        scenarios.add("liquidation_1d")
    if remaining_days <= 3:
        scenarios.add("liquidation_3d")
    if remaining_days <= 6:
        scenarios.add("liquidation_6d")
    if any(op in MAINTENANCE_OPS for op in unit_ops):
        scenarios.add("maintenance_profitable")
    elif tiles.get("maintenance_due", 0):
        scenarios.add("maintenance_unprofitable")
    if any(op in HARVEST_OPS for op in unit_ops):
        scenarios.add("harvest_ready")
    if any(op in INVESTMENT_MARKET_OPS for op in market_ops) or "PLANT" in unit_ops:
        scenarios.add("short_investment" if remaining_days <= 6 else "medium_investment")
        if int(observation.get("day", 0)) <= 5:
            scenarios.add("early_expansion")
    if tiles.get("weeds", 0) or tiles.get("neglected", 0):
        scenarios.add("recovery")

    facts = {
        "product_units": product_units,
        "seed_units": seed_units,
        "plants": tiles.get("plants", 0),
        "animals": tiles.get("animals", 0),
        "weeds": tiles.get("weeds", 0),
        "maintenance_due": tiles.get("maintenance_due", 0),
        "unlocked_tiles": tiles.get("unlocked_tiles", 0),
        "carrot_price_ratio": ratios["CARROT"],
        "tomato_price_ratio": ratios["TOMATO"],
        "egg_price_ratio": ratios["EGG"],
    }
    return facts, sorted(scenarios), unit_ops, market_ops


def episode_rows(source: str, episode: dict[str, Any]) -> Iterator[dict[str, Any]]:
    steps = episode["steps"]
    rewards = [float(value) for value in episode["rewards"]]
    winner_money = max(rewards)
    names = agent_names(episode)
    info = episode.get("info") or {}
    episode_id = str(info.get("EpisodeId", episode.get("id", "unknown")))
    seed = info.get("seed", (episode.get("configuration") or {}).get("seed", ""))
    turns_per_day = int((episode.get("configuration") or {}).get("turnsPerDay", 24))
    module_version = str(episode.get("module_version", ""))
    for turn in range(len(steps) - 1):
        remaining_turns = len(steps) - 1 - turn
        for player in range(2):
            observation = steps[turn][player]["observation"]
            action = steps[turn + 1][player].get("action") or {}
            facts, scenarios, unit_ops, market_ops = classify_state(
                observation, action, player, remaining_turns, turns_per_day
            )
            farms = observation.get("farms") or []
            farm = farms[player] if player < len(farms) and isinstance(farms[player], dict) else {}
            row = {
                "state_key": f"{episode_id}:{turn}:{player}",
                "episode_id": episode_id,
                "source": source,
                "module_version": module_version,
                "seed": seed,
                "player": player,
                "agent": names[player],
                "final_money": rewards[player],
                "winner": int(rewards[player] == winner_money),
                "turn": turn,
                "day": observation.get("day", turn // turns_per_day),
                "hour": observation.get("hour", turn % turns_per_day),
                "remaining_turns": remaining_turns,
                "remaining_days": remaining_turns / max(1, turns_per_day),
                "money": farm.get("money", 0),
                "expert_unit_ops": ",".join(unit_ops),
                "expert_market_ops": ",".join(market_ops),
                "scenarios": ",".join(scenarios),
                **facts,
            }
            yield row


def write_index(args: argparse.Namespace) -> dict[str, Any]:
    paths = expand_inputs(args.inputs)
    if not paths:
        raise SystemExit("no replay inputs found")
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    counts = collections.Counter()
    skipped = collections.Counter()
    agents = collections.Counter()
    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t")
        writer.writeheader()
        for source, episode in iter_replays(paths):
            reason = validate_episode(episode, version_tuple(args.min_version))
            if reason:
                skipped[reason] += 1
                continue
            counts["episodes"] += 1
            for row in episode_rows(source, episode):
                scenarios = row["scenarios"].split(",") if row["scenarios"] else []
                if args.only_scenarios and not scenarios:
                    continue
                writer.writerow(row)
                counts["rows"] += 1
                agents[row["agent"]] += 1
                for scenario in scenarios:
                    counts[f"scenario/{scenario}"] += 1
    summary = {
        "format": "kaggriculture_replay_state_index_v1",
        "output": str(output),
        "inputs": len(paths),
        "counts": dict(sorted(counts.items())),
        "skipped": dict(sorted(skipped.items())),
        "agents": dict(agents.most_common()),
        "warning": "Index rows are replay locators, not resumable KGState snapshots.",
    }
    summary_path = pathlib.Path(args.summary or f"{output}.summary.json")
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="Replay JSON/JSON.GZ/ZIP files, directories, or globs")
    parser.add_argument("--output", required=True, help="Output TSV state index")
    parser.add_argument("--summary", help="Output JSON summary (default: OUTPUT.summary.json)")
    parser.add_argument("--min-version", default="1.32.0")
    parser.add_argument("--only-scenarios", action="store_true", help="Omit rows without a scenario tag")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    summary = write_index(parse_args(argv))
    print(json.dumps(summary["counts"], indent=2, sort_keys=True))
    if summary["skipped"]:
        print("skipped:", json.dumps(summary["skipped"], sort_keys=True), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
