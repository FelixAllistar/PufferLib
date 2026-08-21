#!/usr/bin/env python3
"""Harvest public Kaggriculture agents into auditable action tapes.

The public kernels are Python submissions, while our BC/native pipeline uses
the compact ``farmer``/``hands``/``market`` action tape format.  This script
keeps that boundary explicit: it runs each source in the pinned local
Kaggle-environments version, writes one JSON tape per seed, and records the
terminal/economic behavior beside it.  It intentionally does not mutate the
training config or install a bot in the C/CUDA environment.

The K320 source contains several public route tapes.  ``--k320-routes`` can
force one route at a time, which gives us several genuinely different expert
datasets from the first notebook rather than pretending duplicate wrappers
are independent policies.
"""

from __future__ import annotations

import argparse
import collections
import importlib.util
import json
import pathlib
import statistics
import sys
from typing import Any, Callable

from kaggle_environments import make


DEFAULT_SPECS = {
    "k320": "kaggriculture-rank-your-agent_rank.py",
    "e279": "kaggriculture-3000-socre_score.py",
    "x544": "kaggriculture-x544-nah-i-d-win_SOURCE_B85.py",
    "v16": "v16-rc5-high-score-8c-4s-premium-market-lead_cell6_main.py",
    "c166": "kaggriculture-breaking-the-tie-2883-score_cell1_teacher_agent.py",
}
K320_ROUTES = (
    "10c4s_3q",
    "8c6s_3q",
    "6c8s_3q",
    "6c12s_4q_first_yarn",
    "6c12s_4q_second_yarn",
)


def _load(path: pathlib.Path, name: str):
    spec = importlib.util.spec_from_file_location(f"kag_public_{name}", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _pass(_obs: dict[str, Any]) -> dict[str, Any]:
    return {"farmer": ["PASS"], "hands": [], "market": []}


def _tile_stats(farm: dict[str, Any]) -> collections.Counter[str]:
    counts: collections.Counter[str] = collections.Counter()
    for row in farm.get("tiles", []) or []:
        for tile in row if isinstance(row, list) else [row]:
            if not isinstance(tile, dict):
                continue
            kind = tile.get("kind")
            if kind:
                counts[str(kind)] += 1
            for key in ("crop", "animal"):
                value = tile.get(key)
                if value:
                    counts[f"{key}:{value}"] += 1
    return counts


def _normal_action(action: Any, expected_hands: int) -> dict[str, Any]:
    """Copy an agent action without retaining mutable notebook state."""
    action = action if isinstance(action, dict) else {}
    farmer = list(action.get("farmer") or ["PASS"])
    hands = [list(x or ["PASS"]) for x in (action.get("hands") or [])]
    hands = (hands + [["PASS"] for _ in range(max(0, expected_hands - len(hands)))])[
        :expected_hands
    ]
    market = [list(x) for x in (action.get("market") or []) if x]
    return {"farmer": farmer, "hands": hands, "market": market}


def _route_agent(module, route: str | None) -> Callable[[dict[str, Any]], dict[str, Any]]:
    if route is None:
        return module.agent
    if not hasattr(module, "_kawa_route_label"):
        raise ValueError("--k320-routes is only valid for the K320 source")

    # K320's agent calls this function for every observation.  Force a route
    # while retaining all its weed/feed/market guards and layout fallback.
    original_label = module._kawa_route_label
    original_layout = getattr(module, "_kawa_use_legacy_layout", None)

    def wrapped(obs):
        # The same imported module hosts all route wrappers.  Install the
        # route only for this call so a later wrapper cannot silently change
        # this expert's policy.
        module._kawa_route_label = lambda _obs, _route=route: _route
        if original_layout is not None:
            module._kawa_use_legacy_layout = lambda _obs: False
        return module.agent(obs)

    # Keep references on the closure so the source remains alive; restoration
    # is unnecessary because one module instance is used for one route.
    wrapped.__name__ = f"k320_{route}"
    wrapped._original_label = original_label  # type: ignore[attr-defined]
    return wrapped


def _trajectory(env, player: int = 0) -> list[dict[str, Any]]:
    tape: list[dict[str, Any]] = []
    for step in env.steps:
        row = step[player] if len(step) > player else {}
        action = row.get("action") or {}
        obs = row.get("observation") or {}
        hands = ((obs.get("farms") or [{}])[player].get("hands") or [])
        tape.append(_normal_action(action, len(hands)))
    if len(tape) < 720:
        tape.extend(
            {"farmer": ["PASS"], "hands": [], "market": []}
            for _ in range(720 - len(tape))
        )
    return tape[:720]


def _summary(env, player: int) -> dict[str, Any]:
    rows = [step[player] for step in env.steps]
    final = rows[-1]
    obs = final.get("observation") or {}
    farms = obs.get("farms") or [{}]
    farm = farms[player] if len(farms) > player else {}
    stats = _tile_stats(farm)
    counts: collections.Counter[str] = collections.Counter()
    market: collections.Counter[str] = collections.Counter()
    sold: collections.Counter[str] = collections.Counter()
    for row in rows:
        action = row.get("action") or {}
        for command in [action.get("farmer") or [], *(action.get("hands") or [])]:
            if command:
                counts[str(command[0])] += 1
        for command in action.get("market") or []:
            if not command:
                continue
            market[str(command[0])] += 1
            if len(command) >= 3 and command[0] == "SELL":
                sold[str(command[1])] += int(command[2] or 0)
    return {
        "money": float(final.get("reward") or 0),
        "final_plants": stats.get("PLANT", 0),
        "final_animals": stats.get("PASTURE", 0),
        "final_crops": {k[5:]: v for k, v in stats.items() if k.startswith("crop:")},
        "final_animals_by_type": {
            k[7:]: v for k, v in stats.items() if k.startswith("animal:")
        },
        "farmer_ops": dict(counts),
        "market_ops": dict(market),
        "sold": dict(sold),
        "care": counts.get("CARE", 0),
        "feed": counts.get("FEED", 0),
        "water": counts.get("WATER", 0),
        "harvest": counts.get("HARVEST", 0),
    }


def _run(agent, opponent, seed: int) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    env = make(
        "kaggriculture",
        configuration={"episodeSteps": 720, "seed": seed},
        debug=False,
    )
    env.run([agent, opponent])
    return _trajectory(env), _summary(env, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--seeds", nargs="+", type=int, default=[1234, 1235, 20260])
    parser.add_argument("--agents", nargs="+", default=list(DEFAULT_SPECS))
    parser.add_argument("--k320-routes", nargs="+", choices=K320_ROUTES)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    modules = {
        name: _load(args.source_dir / filename, name)
        for name, filename in DEFAULT_SPECS.items()
        if name in args.agents or (name == "k320" and args.k320_routes)
    }
    jobs: list[tuple[str, Callable]] = []
    for name in args.agents:
        if name == "k320" and args.k320_routes:
            jobs.extend((f"k320_{route}", _route_agent(modules["k320"], route)) for route in args.k320_routes)
        elif name in modules:
            jobs.append((name, modules[name].agent))

    manifest: list[dict[str, Any]] = []
    for label, agent in jobs:
        for seed in args.seeds:
            tape, summary = _run(agent, _pass, seed)
            stem = f"{label}_seed{seed}"
            (args.out / f"{stem}.json").write_text(json.dumps(tape, separators=(",", ":")) + "\n")
            manifest.append({"agent": label, "seed": seed, **summary, "tape": f"{stem}.json"})
            print(
                f"{label:28s} seed={seed:<5d} money={summary['money']:9.1f} "
                f"plants={summary['final_plants']:<2d} animals={summary['final_animals']:<2d} "
                f"care={summary['care']:<3d} feed={summary['feed']:<3d} "
                f"water={summary['water']:<3d} harvest={summary['harvest']:<3d}",
                flush=True,
            )
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    # Compact family-level report makes it obvious when a notebook wrapper is
    # a duplicate or when a route only changes market timing.
    grouped: dict[str, list[float]] = collections.defaultdict(list)
    for row in manifest:
        grouped[row["agent"]].append(float(row["money"]))
    report = {
        key: {
            "n": len(values),
            "money_mean": statistics.mean(values),
            "money_min": min(values),
            "money_max": max(values),
        }
        for key, values in grouped.items()
    }
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
