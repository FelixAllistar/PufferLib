#!/usr/bin/env python3
"""Compare macro checkpoints at fixed turns of a real 720-turn rollout.

Unlike shortening ``episodeSteps``, this keeps the policy's remaining-time
features honest.  Each checkpoint is run deterministically in both seats for
two seeds against PASS.  The report is an engineering gate for opening
transfer, not a competitive evaluation.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import pathlib
from collections import Counter, defaultdict
from typing import Any

from kaggle_environments import make


def _load_agent(source: pathlib.Path, model: pathlib.Path, tag: str):
    os.environ["PUFFERLIB_MODEL_PATH"] = str(model.resolve())
    os.environ["PUFFERLIB_DETERMINISTIC"] = "1"
    spec = importlib.util.spec_from_file_location(f"kag_opening_{tag}", source)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _pass_action(obs: dict[str, Any]) -> dict[str, Any]:
    player = int(obs["player"])
    return {
        "farmer": ["PASS"],
        "hands": [["PASS"] for _ in obs["farms"][player]["hands"]],
        "market": [],
    }


def _farm_metrics(farm: dict[str, Any], actions: Counter[str]) -> dict[str, float]:
    plants = animals = structures = weeds = empty = 0
    for row in farm["tiles"]:
        for tile in row:
            if tile is None:
                empty += 1
            elif tile == "WEED":
                weeds += 1
            elif isinstance(tile, dict):
                kind = tile.get("kind")
                plants += int(kind == "PLANT")
                structures += int(kind in ("COOP", "PASTURE"))
                animals += int(kind in ("COOP", "PASTURE") and bool(tile.get("animal")))
    result = {
        "money": float(farm["money"]),
        "land": float(len(farm.get("unlocked_quadrants", ()))),
        "hands": float(len(farm.get("hands", ()))),
        "plants": float(plants),
        "animals": float(animals),
        "structures": float(structures),
        "weeds": float(weeds),
        "empty_tiles": float(empty),
    }
    for key in (
        "BUY_LAND", "BUY_SEED", "BUY_ANIMAL", "SELL", "HIRE",
        "PLANT", "PLACE", "FEED", "WATER", "CARE", "HARVEST",
    ):
        result[key.lower()] = float(actions.get(key, 0))
    return result


def _run(module, seed: int, seat: int, checkpoints: set[int]) -> dict[int, dict[str, float]]:
    env = make(
        "kaggriculture", configuration={"episodeSteps": 720, "seed": seed},
        debug=True,
    )
    env.reset()
    module._MODEL.reset()
    actions: Counter[str] = Counter()
    snapshots: dict[int, dict[str, float]] = {}
    for turn in range(1, 720):
        joint = []
        for player in range(2):
            obs = json.loads(json.dumps(env.state[player]["observation"]))
            action = module.agent(obs) if player == seat else _pass_action(obs)
            if player == seat:
                for command in [action.get("farmer", ["PASS"]), *action.get("hands", ())]:
                    if command:
                        actions[str(command[0]).upper()] += 1
                for order in action.get("market", ()):
                    if order:
                        actions[str(order[0]).upper()] += 1
            joint.append(action)
        env.step(joint)
        if turn in checkpoints:
            farm = env.state[seat]["observation"]["farms"][seat]
            snapshots[turn] = _farm_metrics(farm, actions)
    return snapshots


def evaluate(
    models: list[pathlib.Path], labels: list[str], source: pathlib.Path,
    turns: list[int], seeds: list[int],
) -> dict[str, Any]:
    checkpoints = set(turns)
    output: dict[str, Any] = {
        "format": "kaggriculture_macro_opening_rollout_v1",
        "episode_steps": 720,
        "opponent": "PASS",
        "turns": turns,
        "seeds": seeds,
        "models": {},
    }
    for index, (model, label) in enumerate(zip(models, labels)):
        module = _load_agent(source, model, f"{index}_{label}")
        runs = []
        aggregate: dict[int, dict[str, list[float]]] = defaultdict(
            lambda: defaultdict(list)
        )
        for seed in seeds:
            for seat in (0, 1):
                snapshots = _run(module, seed, seat, checkpoints)
                runs.append({"seed": seed, "seat": seat, "snapshots": snapshots})
                for turn, metrics in snapshots.items():
                    for key, value in metrics.items():
                        aggregate[turn][key].append(value)
        means = {
            str(turn): {
                key: sum(values) / len(values)
                for key, values in sorted(metrics.items())
            }
            for turn, metrics in sorted(aggregate.items())
        }
        output["models"][label] = {
            "path": str(model), "means": means, "runs": runs,
        }
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", nargs="+", type=pathlib.Path)
    parser.add_argument("--labels", help="comma-separated labels")
    parser.add_argument(
        "--source", type=pathlib.Path,
        default=pathlib.Path(__file__).with_name("submission") / "main.py",
    )
    parser.add_argument("--turns", default="24,60,180,719")
    parser.add_argument("--seeds", default="7,42")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    labels = args.labels.split(",") if args.labels else [path.stem for path in args.models]
    if len(labels) != len(args.models):
        parser.error("--labels count must match model count")
    turns = sorted({int(value) for value in args.turns.split(",")})
    seeds = [int(value) for value in args.seeds.split(",")]
    if not turns or turns[0] < 1 or turns[-1] > 719:
        parser.error("--turns must be between 1 and 719")
    result = evaluate(args.models, labels, args.source, turns, seeds)
    payload = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload + "\n")
    print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
