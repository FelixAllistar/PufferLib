#!/usr/bin/env python3
"""Fit replay-state win and money-margin estimators.

This is deliberately separate from ``fit_elite_economic_values.py``.  The
economic fit estimates realizable asset value; this fit answers a different
question: given the public state (and the player's private inventory), how
likely is this player to finish ahead?  It writes an auditable ridge model
for later diagnostics or terminal shaping.  It does not alter the simulator
or enable a reward by itself.

The sufficient statistics are accumulated online, so the full replay archive
does not need to be expanded into a large feature matrix.  Each episode is
weighted equally, rather than allowing 720 repeated frames from a single
game to dominate the fit.
"""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any

import numpy as np

from import_elite_replays import EXPECTED_STEPS, _expand_inputs, _iter_replays, _validate_episode, _version_tuple


STARTING_MONEY = 3000.0
PRODUCTS = (
    "WHEAT", "CARROT", "TOMATO", "STRAWBERRY", "MELON",
    "EGG", "MILK", "WOOL", "FERTILIZER",
)
FEATURE_NAMES = (
    "bias",
    "day_fraction",
    "cash_self_norm",
    "cash_gap_norm",
    "land_self_norm",
    "land_gap_norm",
    "plants_self_norm",
    "plants_gap_norm",
    "animals_self_norm",
    "animals_gap_norm",
    "yield_self_norm",
    "yield_gap_norm",
    "seed_count_norm",
    "shed_value_self_norm",
    "market_inventory_gap_norm",
)


def _tile_stats(farm: dict[str, Any]) -> tuple[float, float, float]:
    plants = animals = yield_units = 0.0
    for row in farm.get("tiles", []):
        if not isinstance(row, list):
            continue
        for tile in row:
            if not isinstance(tile, dict):
                continue
            kind = str(tile.get("kind", "")).upper()
            if kind == "PLANT":
                plants += 1.0
            elif kind in {"COOP", "PASTURE"} and tile.get("animal"):
                animals += 1.0
            try:
                yield_units += max(0.0, float(tile.get("yield_units", 0.0)))
            except (TypeError, ValueError):
                pass
    return plants, animals, yield_units


def _feature_vector(observation: dict[str, Any], player: int) -> np.ndarray:
    farms = observation.get("farms", [])
    if not isinstance(farms, list) or len(farms) < 2:
        raise ValueError("observation has no two farms")
    own = farms[player]
    other = farms[1 - player]
    own_plants, own_animals, own_yield = _tile_stats(own)
    other_plants, other_animals, other_yield = _tile_stats(other)
    own_money = float(own.get("money", STARTING_MONEY))
    other_money = float(other.get("money", STARTING_MONEY))
    own_land = float(len(own.get("unlocked_quadrants", [])))
    other_land = float(len(other.get("unlocked_quadrants", [])))

    private = observation.get("private", {}) or {}
    seeds = private.get("seeds", {}) or {}
    seed_count = sum(max(0.0, float(value)) for value in seeds.values())
    prices = observation.get("market", {}).get("prices", {}) or {}
    shed = private.get("shed", {}) or {}
    shed_value = 0.0
    for product in PRODUCTS:
        try:
            shed_value += max(0.0, float(shed.get(product, 0.0))) * max(
                0.0, float(prices.get(product, 0.0))
            )
        except (TypeError, ValueError):
            pass
    inventory = observation.get("market", {}).get("inventory", {}) or {}
    market_gap = 0.0
    # This is a public market-pressure feature, not opponent private state.
    for product in PRODUCTS:
        try:
            market_gap += float(inventory.get(product, 0.0)) / 10000.0
        except (TypeError, ValueError):
            pass

    day = float(observation.get("day", 0.0))
    return np.asarray([
        1.0,
        np.clip(day / 30.0, 0.0, 1.0),
        own_money / STARTING_MONEY,
        (own_money - other_money) / STARTING_MONEY,
        own_land / 4.0,
        (own_land - other_land) / 4.0,
        own_plants / 100.0,
        (own_plants - other_plants) / 100.0,
        own_animals / 100.0,
        (own_animals - other_animals) / 100.0,
        own_yield / 500.0,
        (own_yield - other_yield) / 500.0,
        seed_count / 100.0,
        shed_value / STARTING_MONEY,
        market_gap,
    ], dtype=np.float64)


def _solve_ridge(xtx: np.ndarray, xty: np.ndarray, ridge: float) -> np.ndarray:
    regularizer = np.eye(xtx.shape[0], dtype=np.float64) * ridge
    regularizer[0, 0] = 0.0  # do not shrink the intercept
    try:
        return np.linalg.solve(xtx + regularizer, xty)
    except np.linalg.LinAlgError:
        return np.linalg.pinv(xtx + regularizer) @ xty


def _write_ini(path: pathlib.Path, output_name: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "# Diagnostic only; the simulator does not consume this automatically.\n"
        f"# win_model_path = {output_name}\n"
        "# Keep win estimation separate from the positive economic value fit.\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", help="replay JSON/JSON.GZ/ZIP paths or globs")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--ini-output", type=pathlib.Path)
    parser.add_argument("--minimum-version", default="1.32.7")
    parser.add_argument("--exact-version", default="1.32.7")
    parser.add_argument("--steps", type=int, default=EXPECTED_STEPS)
    parser.add_argument("--stride", type=int, default=8)
    parser.add_argument("--minimum-final-money", type=float, default=0.0)
    parser.add_argument("--ridge", type=float, default=1e-2)
    args = parser.parse_args()
    if args.steps < 1 or args.stride < 1:
        raise SystemExit("--steps and --stride must be positive")
    if args.ridge < 0.0:
        raise SystemExit("--ridge must be nonnegative")

    paths = _expand_inputs(args.inputs)
    minimum_version = _version_tuple(args.minimum_version)
    exact_version = _version_tuple(args.exact_version)
    dim = len(FEATURE_NAMES)
    xtx = np.zeros((dim, dim), dtype=np.float64)
    xty_win = np.zeros(dim, dtype=np.float64)
    xty_margin = np.zeros(dim, dtype=np.float64)
    episodes = trajectories = states = skipped = 0
    wins = draws = losses = 0

    for _source, episode in _iter_replays(paths):
        reason = _validate_episode(episode, minimum_version, args.steps, exact_version)
        if reason is not None:
            skipped += 1
            continue
        rewards = [float(value) for value in episode["rewards"]]
        if len(rewards) < 2:
            skipped += 1
            continue
        if max(rewards) < args.minimum_final_money:
            skipped += 1
            continue
        margin = (rewards[0] - rewards[1]) / STARTING_MONEY
        if rewards[0] > rewards[1]:
            wins += 1
        elif rewards[0] < rewards[1]:
            losses += 1
        else:
            draws += 1
        episodes += 1
        frame_count = min(args.steps, len(episode.get("steps", [])))
        frame_indices = list(range(0, frame_count, args.stride))
        if not frame_indices:
            continue
        # Equal episode weighting; both player perspectives provide labels.
        weight = 1.0 / (2.0 * len(frame_indices))
        for turn in frame_indices:
            frames = episode["steps"][turn]
            if not isinstance(frames, list) or len(frames) < 2:
                continue
            for player in (0, 1):
                try:
                    observation = frames[player]["observation"]
                    vector = _feature_vector(observation, player)
                except (KeyError, TypeError, ValueError, IndexError):
                    continue
                target_win = 1.0 if rewards[player] > rewards[1 - player] else (
                    0.0 if rewards[player] < rewards[1 - player] else 0.5
                )
                target_margin = (rewards[player] - rewards[1 - player]) / STARTING_MONEY
                xtx += weight * np.outer(vector, vector)
                xty_win += weight * vector * target_win
                xty_margin += weight * vector * target_margin
                states += 1
        trajectories += 2

    if episodes == 0 or states == 0:
        raise SystemExit("no compatible replay states accepted")
    win_coef = _solve_ridge(xtx, xty_win, args.ridge)
    margin_coef = _solve_ridge(xtx, xty_margin, args.ridge)

    fit = {
        "schema_version": 1,
        "model": "public_state_ridge_win_and_margin",
        "input_archives": [path.name for path in paths],
        "minimum_version": args.minimum_version,
        "exact_version": args.exact_version,
        "episodes": episodes,
        "trajectories": trajectories,
        "states": states,
        "skipped_episodes": skipped,
        "stride": args.stride,
        "ridge": args.ridge,
        "starting_money": STARTING_MONEY,
        "outcome_counts": {"player0_wins": wins, "draws": draws, "player1_wins": losses},
        "feature_names": list(FEATURE_NAMES),
        "win_probability_linear_coefficients": [float(x) for x in win_coef],
        "money_margin_linear_coefficients": [float(x) for x in margin_coef],
        "usage": {
            "win_probability": "clip(dot(features, win_probability_linear_coefficients), 0, 1)",
            "money_margin": "dot(features, money_margin_linear_coefficients)",
            "warning": "diagnostic fit; do not add to reward until cross-validated",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(fit, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    ini_output = args.ini_output or args.output.with_suffix(".ini")
    _write_ini(ini_output, args.output.name)
    print(json.dumps({
        "episodes": episodes,
        "trajectories": trajectories,
        "states": states,
        "outcome_counts": fit["outcome_counts"],
        "output": str(args.output),
        "ini_output": str(ini_output),
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
