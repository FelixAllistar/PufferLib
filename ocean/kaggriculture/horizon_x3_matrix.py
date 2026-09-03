#!/usr/bin/env python3
"""Run a narrow, reproducible Kaggriculture recurrent-horizon comparison.

The three trials keep roughly 131k rollout transitions per PPO update while
changing contiguous recurrent horizon.  Every trial has its own checkpoint
directory and is evaluated at common training milestones against one fixed
public-bot distribution.  The source config supplies rewards and all settings
not explicitly listed in ``training_overrides``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
from typing import Any

from qd_sweep import behavior_descriptor, parse_eval_json


REPO = Path(__file__).resolve().parents[2]
SPECS = ((256, 512), (512, 256), (720, 192))  # (horizon, agents)
MILESTONES = (50_000_000, 100_000_000, 200_000_000, 300_000_000, 500_000_000)


def override(key: str, value: Any) -> str:
    if isinstance(value, float):
        return f"{key}={value:.10g}"
    return f"{key}={value}"


def checkpoint_step(path: Path) -> int:
    try:
        return int(path.stem)
    except ValueError:
        return -1


def milestone_checkpoints(run_dir: Path, steps: int) -> list[Path]:
    checkpoints = sorted(
        (p for p in run_dir.glob("*.bin") if checkpoint_step(p) >= 0),
        key=checkpoint_step,
    )
    if not checkpoints:
        return []
    targets = [value for value in MILESTONES if value <= steps]
    if not targets or targets[-1] != steps:
        targets.append(steps)
    selected: dict[Path, None] = {}
    for target in targets:
        selected[min(checkpoints, key=lambda p: abs(checkpoint_step(p) - target))] = None
    selected[checkpoints[-1]] = None
    return sorted(selected, key=checkpoint_step)


def training_overrides(args: argparse.Namespace, run_id: str, horizon: int,
        agents: int) -> dict[str, Any]:
    transitions = horizon * agents
    checkpoint_interval = max(1, round(25_000_000 / transitions))
    return {
        "base.run_id": run_id,
        "base.load_model_path": "None",
        "base.load_enemy_model_path": "None",
        "base.checkpoint_interval": checkpoint_interval,
        "base.seed": args.seed,
        "policy.hidden_size": 256,
        "policy.num_layers": 3,
        "vec.total_agents": agents,
        "vec.frozen_bank_hidden_size": 256,
        "vec.frozen_bank_num_layers": 3,
        "train.total_timesteps": args.steps,
        "train.seed": args.seed,
        "train.horizon": horizon,
        "train.minibatch_size": args.minibatch,
        "train.learning_rate": args.learning_rate,
    }


def evaluation_overrides(args: argparse.Namespace, checkpoint: Path,
        deterministic: int) -> dict[str, Any]:
    return {
        "base.load_model_path": str(checkpoint),
        "base.num_games": args.eval_games,
        "base.eval_agents": min(128, max(16, args.eval_games)),
        "base.eval_deterministic": deterministic,
        "base.seed": args.eval_seed,
        "policy.hidden_size": 256,
        "policy.num_layers": 3,
        "vec.num_frozen_banks": 0,
        "vec.frozen_bank_hidden_size": 256,
        "vec.frozen_bank_num_layers": 3,
        "vec.frozen_bank_pct": 0,
        "selfplay.enabled": 0,
        "env.macro_mode": 2,
        "env.episode_steps": 720,
        "env.seed": args.eval_seed,
        "env.reset_state_prob": 0,
        "env.curriculum_enabled": 0,
        "env.bot_opponent_fraction": 1,
        "env.bot_pass_fraction": 0,
        "env.bot_top_fraction": 0.35,
        "env.bot_rules_fraction": 0.20,
        "env.bot_script_fraction": 0.20,
        "env.bot_adaptive_fraction": 0.25,
    }


def run_command(command: list[str], *, log: Path | None = None,
        inherit_output: bool = False) -> str:
    if inherit_output:
        result = subprocess.run(command, cwd=REPO)
        if result.returncode:
            raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}")
        return ""
    result = subprocess.run(command, cwd=REPO, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if log is not None:
        log.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); see {log}")
    return result.stdout


def write_summary(output: Path, records: list[dict[str, Any]]) -> None:
    columns = ("run_id", "horizon", "agents", "transitions", "checkpoint_step",
               "deterministic", "money", "win_rate", "land", "plants", "animals",
               "gdp", "production")
    with (output / "summary.tsv").open("w") as handle:
        handle.write("\t".join(columns) + "\n")
        for row in sorted(records, key=lambda r: (r["run_id"], r["checkpoint_step"],
                                                  r["deterministic"])):
            handle.write("\t".join(str(row[column]) for column in columns) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path("logs/kaggriculture/horizon_x3_256_v1"))
    parser.add_argument("--steps", type=int, default=500_000_000)
    parser.add_argument("--minibatch", type=int, default=2048)
    parser.add_argument("--learning-rate", type=float, default=7e-4)
    parser.add_argument("--eval-games", type=int, default=128)
    parser.add_argument("--seed", type=int, default=707)
    parser.add_argument("--eval-seed", type=int, default=9001)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    output = args.output if args.output.is_absolute() else REPO / args.output
    output.mkdir(parents=True, exist_ok=True)
    config = REPO / "config/kaggriculture.ini"
    snapshot = output / "source_config.ini"
    if not snapshot.exists():
        shutil.copy2(config, snapshot)
    metadata = {
        "format": "kaggriculture_horizon_x3_matrix_v1",
        "source_config_sha256": hashlib.sha256(snapshot.read_bytes()).hexdigest(),
        "steps": args.steps,
        "minibatch": args.minibatch,
        "learning_rate": args.learning_rate,
        "seed": args.seed,
        "eval_seed": args.eval_seed,
        "eval_games": args.eval_games,
        "specs": [{"horizon": h, "agents": a, "transitions": h * a}
                  for h, a in SPECS],
    }
    (output / "experiment.json").write_text(json.dumps(metadata, indent=2) + "\n")

    records_path = output / "evaluations.jsonl"
    records: list[dict[str, Any]] = []
    completed: set[tuple[str, int, int]] = set()
    if records_path.exists():
        for line in records_path.read_text().splitlines():
            row = json.loads(line)
            records.append(row)
            completed.add((row["run_id"], row["checkpoint_step"], row["deterministic"]))

    prefix = output.name
    for horizon, agents in SPECS:
        run_id = f"{prefix}_h{horizon}_a{agents}_m{args.minibatch}_lr{args.learning_rate:g}_s{args.seed}"
        run_dir = REPO / "checkpoints/kaggriculture" / run_id
        done_marker = output / f"{run_id}.trained"
        train_overrides = training_overrides(args, run_id, horizon, agents)
        train_command = [str(REPO / "puffer"), "train", "kaggriculture"]
        train_command.extend(override(k, v) for k, v in train_overrides.items())
        (output / f"{run_id}.command.txt").write_text(" ".join(train_command) + "\n")

        if args.dry_run:
            print("DRY", " ".join(train_command), flush=True)
            continue
        if not done_marker.exists():
            if run_dir.exists():
                raise FileExistsError(
                    f"refusing ambiguous resume from existing {run_dir}; "
                    "remove it or choose a new --output after inspecting it")
            print(f"TRAIN {run_id}: horizon={horizon} agents={agents} "
                  f"transitions={horizon * agents} steps={args.steps}", flush=True)
            run_command(train_command, inherit_output=True)
            done_marker.write_text("complete\n")

        checkpoints = milestone_checkpoints(run_dir, args.steps)
        if not checkpoints:
            raise RuntimeError(f"no checkpoints produced for {run_id}")
        for checkpoint in checkpoints:
            step = checkpoint_step(checkpoint)
            # Stochastic evaluation at every milestone; deterministic evaluation
            # only for the final checkpoint to limit evaluation cost.
            modes = (0, 1) if checkpoint == checkpoints[-1] else (0,)
            for deterministic in modes:
                key = (run_id, step, deterministic)
                if key in completed:
                    continue
                command = [str(REPO / "puffer"), "eval", "kaggriculture"]
                command.extend(override(k, v) for k, v in
                               evaluation_overrides(args, checkpoint, deterministic).items())
                log = output / f"eval_{run_id}_{step}_d{deterministic}.log"
                stdout = run_command(command, log=log)
                metrics = parse_eval_json(stdout)
                descriptor = behavior_descriptor(metrics)
                row = {
                    "run_id": run_id,
                    "horizon": horizon,
                    "agents": agents,
                    "transitions": horizon * agents,
                    "checkpoint": str(checkpoint.relative_to(REPO)),
                    "checkpoint_step": step,
                    "deterministic": deterministic,
                    "money": float(metrics.get("env/money", float("nan"))),
                    "win_rate": float(metrics.get("env/win_rate", float("nan"))),
                    "land": float(metrics.get("env/land_purchases", float("nan"))),
                    "plants": float(metrics.get("env/plants_alive", float("nan"))),
                    "animals": float(metrics.get("env/animals_alive", float("nan"))),
                    "gdp": float(metrics.get("env/gdp", float("nan"))),
                    "production": float(metrics.get("env/production_units", float("nan"))),
                    "descriptor": descriptor,
                    "metrics": metrics,
                }
                with records_path.open("a") as handle:
                    handle.write(json.dumps(row, sort_keys=True) + "\n")
                records.append(row)
                completed.add(key)
                write_summary(output, records)
                print(f"EVAL {run_id} step={step} deterministic={deterministic} "
                      f"money={row['money']:.1f} win={row['win_rate']:.3f} "
                      f"land={row['land']:.2f} plants={row['plants']:.1f} "
                      f"animals={row['animals']:.1f}", flush=True)

    write_summary(output, records)
    if not args.dry_run:
        print(f"COMPLETE: {output / 'summary.tsv'}", flush=True)


if __name__ == "__main__":
    main()
