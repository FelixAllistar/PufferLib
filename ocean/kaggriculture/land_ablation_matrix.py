#!/usr/bin/env python3
"""Run the two controlled Kaggriculture land-reward ablations."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from horizon_x3_matrix import (
    REPO,
    checkpoint_step,
    evaluation_overrides,
    milestone_checkpoints,
    override,
    run_command,
    training_overrides,
)
from qd_sweep import behavior_descriptor, parse_eval_json


VARIANTS = (
    ("exp0_land1", 0.0, 1.0),
    ("exp0_land0", 0.0, 0.0),
)


def write_summary(output: Path, records: list[dict]) -> None:
    columns = (
        "variant", "run_id", "checkpoint_step", "deterministic", "money",
        "win_rate", "land", "plants", "animals", "gdp", "production",
        "sales_revenue", "neglect_deaths", "planting_day_deaths",
    )
    with (output / "summary.tsv").open("w") as handle:
        handle.write("\t".join(columns) + "\n")
        for row in sorted(records, key=lambda r: (
                r["variant"], r["checkpoint_step"], r["deterministic"])):
            handle.write("\t".join(str(row[column]) for column in columns) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path("logs/kaggriculture/land_ablation_256x3_v1"))
    parser.add_argument("--steps", type=int, default=200_000_000)
    parser.add_argument("--eval-games", type=int, default=128)
    parser.add_argument("--seed", type=int, default=707)
    parser.add_argument("--eval-seed", type=int, default=9001)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    # Reuse the common controlled-run helpers with the established H256 setup.
    args.minibatch = 2048
    args.learning_rate = 7e-4

    output = args.output if args.output.is_absolute() else REPO / args.output
    output.mkdir(parents=True, exist_ok=True)
    config = REPO / "config/kaggriculture.ini"
    snapshot = output / "source_config.ini"
    if not snapshot.exists():
        shutil.copy2(config, snapshot)
    metadata = {
        "format": "kaggriculture_land_ablation_v1",
        "source_config_sha256": hashlib.sha256(snapshot.read_bytes()).hexdigest(),
        "architecture": "256x3",
        "horizon": 256,
        "agents": 512,
        "minibatch": 2048,
        "learning_rate": 7e-4,
        "steps": args.steps,
        "eval_games": args.eval_games,
        "seed": args.seed,
        "eval_seed": args.eval_seed,
        "variants": [
            {"name": name, "reward_expansion_scale": expansion,
             "reward_progress_land_scale": land}
            for name, expansion, land in VARIANTS
        ],
    }
    (output / "experiment.json").write_text(json.dumps(metadata, indent=2) + "\n")

    records_path = output / "evaluations.jsonl"
    records: list[dict] = []
    completed: set[tuple[str, int, int]] = set()
    if records_path.exists():
        for line in records_path.read_text().splitlines():
            row = json.loads(line)
            records.append(row)
            completed.add((row["run_id"], row["checkpoint_step"], row["deterministic"]))

    for name, expansion_scale, land_scale in VARIANTS:
        run_id = f"{output.name}_{name}_h256_a512_m2048_lr0.0007_s{args.seed}"
        run_dir = REPO / "checkpoints/kaggriculture" / run_id
        done_marker = output / f"{run_id}.trained"
        overrides = training_overrides(args, run_id, 256, 512)
        overrides.update({
            "env.reward_expansion_scale": expansion_scale,
            "env.reward_progress_land_scale": land_scale,
        })
        command = [str(REPO / "puffer"), "train", "kaggriculture"]
        command.extend(override(key, value) for key, value in overrides.items())
        (output / f"{run_id}.command.txt").write_text(" ".join(command) + "\n")
        if args.dry_run:
            print("DRY", " ".join(command), flush=True)
            continue
        if not done_marker.exists():
            if run_dir.exists():
                raise FileExistsError(f"refusing ambiguous existing run directory: {run_dir}")
            print(f"TRAIN {name}: expansion={expansion_scale:g} land={land_scale:g}",
                  flush=True)
            run_command(command, inherit_output=True)
            done_marker.write_text("complete\n")

        checkpoints = milestone_checkpoints(run_dir, args.steps)
        if not checkpoints:
            raise RuntimeError(f"no checkpoints produced for {run_id}")
        for checkpoint in checkpoints:
            step = checkpoint_step(checkpoint)
            modes = (0, 1) if checkpoint == checkpoints[-1] else (0,)
            for deterministic in modes:
                key = (run_id, step, deterministic)
                if key in completed:
                    continue
                eval_command = [str(REPO / "puffer"), "eval", "kaggriculture"]
                eval_command.extend(override(k, v) for k, v in
                                    evaluation_overrides(args, checkpoint,
                                                         deterministic).items())
                log = output / f"eval_{run_id}_{step}_d{deterministic}.log"
                metrics = parse_eval_json(run_command(eval_command, log=log))
                row = {
                    "variant": name,
                    "run_id": run_id,
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
                    "sales_revenue": float(metrics.get("env/sales_revenue", float("nan"))),
                    "neglect_deaths": float(metrics.get("env/neglect_deaths", float("nan"))),
                    "planting_day_deaths": float(metrics.get(
                        "env/planting_day_deaths", float("nan"))),
                    "descriptor": behavior_descriptor(metrics),
                    "metrics": metrics,
                }
                with records_path.open("a") as handle:
                    handle.write(json.dumps(row, sort_keys=True) + "\n")
                records.append(row)
                completed.add(key)
                write_summary(output, records)
                print(f"EVAL {name} step={step} d={deterministic} "
                      f"money={row['money']:.1f} win={row['win_rate']:.3f} "
                      f"land={row['land']:.2f} plants={row['plants']:.1f} "
                      f"animals={row['animals']:.1f}", flush=True)

    write_summary(output, records)
    if not args.dry_run:
        print(f"COMPLETE: {output / 'summary.tsv'}", flush=True)


if __name__ == "__main__":
    main()
