#!/usr/bin/env python3
"""Quality-diversity search for Kaggriculture training configurations.

Protein optimizes one scalar and can consequently collapse onto one easily
learned strategy.  This driver keeps the best checkpoint in each behavioral
niche instead.  Every trial has an isolated run id, and every retained
checkpoint is evaluated by the native PufferLib evaluator.

The archive axes intentionally describe outcomes, not reward coefficients:
land expansion, crop/animal production mix, and reinvestment intensity.  Cash
is used only to rank policies *within* a niche.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import datetime as dt
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
from typing import Any, Iterable


FORMAT = "kaggriculture_qd_archive_v1"
REPO = Path(__file__).resolve().parents[2]
DEFAULT_FAMILIES = ("balanced", "crop", "animal", "expansion", "liquidator", "sparse")


@dataclasses.dataclass(frozen=True)
class ParamSpec:
    low: float
    high: float
    mode: str = "linear"
    integer: bool = False

    def sample(self, rng: random.Random) -> float | int:
        if self.mode == "log":
            value = math.exp(rng.uniform(math.log(self.low), math.log(self.high)))
        else:
            value = rng.uniform(self.low, self.high)
        return int(round(value)) if self.integer else value

    def mutate(self, value: float | int, rng: random.Random, strength: float) -> float | int:
        if self.mode == "log":
            log_value = math.log(max(float(value), self.low))
            span = math.log(self.high) - math.log(self.low)
            value = math.exp(log_value + rng.gauss(0.0, strength * span))
        else:
            value = float(value) + rng.gauss(0.0, strength * (self.high - self.low))
        value = min(self.high, max(self.low, value))
        return int(round(value)) if self.integer else value


SPECS: dict[str, ParamSpec] = {
    "train.learning_rate": ParamSpec(1e-4, 3e-3, "log"),
    "train.ent_coef": ParamSpec(1e-4, 1e-2, "log"),
    "env.reward_progress_scale": ParamSpec(0.1, 4.0, "log"),
    "env.reward_progress_win_scale": ParamSpec(0.0, 4.0),
    "env.reward_progress_liquidation_days": ParamSpec(0, 8, integer=True),
    "env.reward_progress_seed_scale": ParamSpec(0.5, 3.0, "log"),
    # Actual 128/256 experiments need substantially more teaching pressure
    # than the old nominal [0, 5] sweep to discover production diversity.
    "env.reward_progress_crop_scale": ParamSpec(0.5, 40.0, "log"),
    "env.reward_progress_animal_scale": ParamSpec(0.5, 80.0, "log"),
    "env.reward_progress_product_scale": ParamSpec(0.5, 3.0, "log"),
    "env.reward_progress_land_scale": ParamSpec(0.5, 4.0, "log"),
    "env.reward_expansion_scale": ParamSpec(0.0, 4.0),
}


FAMILY_CENTERS: dict[str, dict[str, float | int]] = {
    "balanced": {
        "train.learning_rate": 7e-4, "train.ent_coef": 8e-4,
        "env.reward_progress_scale": 1.0, "env.reward_progress_win_scale": 1.0,
        "env.reward_progress_liquidation_days": 2,
        "env.reward_progress_seed_scale": 1.0, "env.reward_progress_crop_scale": 5.0,
        "env.reward_progress_animal_scale": 10.0, "env.reward_progress_product_scale": 1.0,
        "env.reward_progress_land_scale": 1.5, "env.reward_expansion_scale": 1.0,
    },
    "crop": {
        "env.reward_progress_crop_scale": 20.0, "env.reward_progress_animal_scale": 2.0,
        "env.reward_progress_seed_scale": 1.5, "env.reward_progress_land_scale": 2.0,
    },
    "animal": {
        "env.reward_progress_crop_scale": 4.0, "env.reward_progress_animal_scale": 45.0,
        "env.reward_progress_product_scale": 1.8, "env.reward_progress_land_scale": 2.0,
    },
    "expansion": {
        "env.reward_progress_crop_scale": 12.0, "env.reward_progress_animal_scale": 25.0,
        "env.reward_progress_land_scale": 3.0, "env.reward_expansion_scale": 3.0,
    },
    "liquidator": {
        "env.reward_progress_crop_scale": 4.0, "env.reward_progress_animal_scale": 8.0,
        "env.reward_progress_product_scale": 2.0,
        "env.reward_progress_liquidation_days": 6,
        "env.reward_progress_win_scale": 2.0,
    },
    "sparse": {
        "env.reward_progress_scale": 0.2, "env.reward_progress_win_scale": 0.0,
        "env.reward_progress_liquidation_days": 0,
        "env.reward_progress_seed_scale": 1.0, "env.reward_progress_crop_scale": 1.0,
        "env.reward_progress_animal_scale": 1.0, "env.reward_progress_product_scale": 1.0,
        "env.reward_progress_land_scale": 1.0, "env.reward_expansion_scale": 0.0,
    },
}


def _base_center() -> dict[str, float | int]:
    return dict(FAMILY_CENTERS["balanced"])


def sample_parameters(rng: random.Random, family: str, *, global_sample: bool,
        parent: dict[str, float | int] | None = None,
        mutation_strength: float = 0.16) -> dict[str, float | int]:
    if global_sample:
        return {name: spec.sample(rng) for name, spec in SPECS.items()}
    center = _base_center()
    center.update(FAMILY_CENTERS.get(family, {}))
    if parent is not None:
        center.update(parent)
    return {
        name: spec.mutate(center.get(name, spec.sample(rng)), rng, mutation_strength)
        for name, spec in SPECS.items()
    }


def _bin(value: float, cuts: tuple[float, float], names: tuple[str, str, str]) -> str:
    if value < cuts[0]:
        return names[0]
    if value < cuts[1]:
        return names[1]
    return names[2]


def behavior_descriptor(metrics: dict[str, float]) -> dict[str, float | str]:
    land = float(metrics.get("env/land_purchases", 0.0))
    crop = max(0.0, float(metrics.get("env/crop_production_units", 0.0)))
    animal = max(0.0, float(metrics.get("env/animal_production_units", 0.0)))
    animal_fraction = animal / max(crop + animal, 1e-9)
    spend = max(0.0, float(metrics.get("env/purchase_spend", 0.0)))
    revenue = max(0.0, float(metrics.get("env/sales_revenue", 0.0)))
    reinvestment = spend / max(revenue, 1.0)
    return {
        "land": land,
        "animal_fraction": animal_fraction,
        "reinvestment": reinvestment,
        "land_bin": _bin(land, (0.5, 1.5), ("compact", "one_expand", "broad")),
        "mix_bin": _bin(animal_fraction, (0.15, 0.55), ("crop", "mixed", "animal")),
        "reinvestment_bin": _bin(
            reinvestment, (0.25, 0.65), ("cash_heavy", "balanced", "growth_heavy")),
    }


def niche_key(descriptor: dict[str, float | str]) -> str:
    return "/".join(str(descriptor[key]) for key in
        ("land_bin", "mix_bin", "reinvestment_bin"))


def parse_eval_json(text: str) -> dict[str, float]:
    for line in reversed(text.splitlines()):
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(data, dict) and any(str(key).startswith("env/") for key in data):
            return {str(key): float(value) for key, value in data.items()
                    if isinstance(value, (int, float))}
    match = re.search(r"(\{[^\n]*\"env/money\"[^\n]*\})", text)
    if match:
        return {str(key): float(value) for key, value in json.loads(match.group(1)).items()
                if isinstance(value, (int, float))}
    raise ValueError("native eval output did not contain an env metrics JSON object")


class Archive:
    def __init__(self, path: Path):
        self.path = path
        self.entries: dict[str, dict[str, Any]] = {}
        if path.exists():
            data = json.loads(path.read_text())
            if data.get("format") != FORMAT:
                raise ValueError(f"unsupported archive format in {path}")
            self.entries = dict(data.get("entries", {}))

    def consider(self, record: dict[str, Any]) -> bool:
        key = str(record["niche"])
        incumbent = self.entries.get(key)
        if incumbent is not None and float(incumbent["quality"]) >= float(record["quality"]):
            return False
        self.entries[key] = record
        return True

    def parents(self) -> list[dict[str, float | int]]:
        return [dict(entry["parameters"]) for entry in self.entries.values()]

    def save(self) -> None:
        payload = {"format": FORMAT, "entries": self.entries}
        temporary = self.path.with_suffix(self.path.suffix + ".tmp")
        temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
        temporary.replace(self.path)
        rows = self.path.with_suffix(".tsv")
        columns = ["niche", "quality", "money", "win_rate", "checkpoint", "trial", "family",
                   "land", "animal_fraction", "reinvestment"]
        with rows.open("w") as handle:
            handle.write("\t".join(columns) + "\n")
            for key, entry in sorted(self.entries.items()):
                descriptor = entry["descriptor"]
                values = {
                    "niche": key, "quality": entry["quality"], "money": entry["money"],
                    "win_rate": entry.get("win_rate", 0.0), "checkpoint": entry["checkpoint"],
                    "trial": entry["trial"], "family": entry["family"],
                    "land": descriptor["land"], "animal_fraction": descriptor["animal_fraction"],
                    "reinvestment": descriptor["reinvestment"],
                }
                handle.write("\t".join(str(values[column]) for column in columns) + "\n")


def _format_override(key: str, value: Any) -> str:
    if isinstance(value, float):
        return f"{key}={value:.10g}"
    return f"{key}={value}"


def fixed_training_overrides(args: argparse.Namespace, run_id: str) -> dict[str, Any]:
    steps_per_epoch = max(1, args.agents * args.horizon)
    train_epochs = max(1, math.ceil(args.steps / steps_per_epoch))
    interval = max(1, train_epochs // max(args.checkpoints_per_run, 1))
    overrides = {
        "base.run_id": run_id,
        "base.load_model_path": "None",
        "base.load_enemy_model_path": "None",
        "base.checkpoint_interval": interval,
        "base.eval_episodes": 4,
        "base.seed": args.seed,
        "policy.hidden_size": args.hidden_size,
        "policy.num_layers": args.layers,
        "vec.total_agents": args.agents,
        "vec.num_frozen_banks": 0,
        "vec.frozen_bank_pct": 0,
        "selfplay.enabled": 0,
        "selfplay.opponent_league": "None",
        "env.macro_mode": 2,
        "env.episode_steps": 720,
        "env.seed": args.seed,
        "env.curriculum_enabled": 0,
        "env.reset_state_prob": 0,
        "env.bot_opponent_fraction": 1,
        "env.bot_pass_fraction": 0,
        "env.bot_top_fraction": 0.35,
        "env.bot_rules_fraction": 0.20,
        "env.bot_script_fraction": 0.20,
        "env.bot_adaptive_fraction": 0.25,
        "env.reward_money_scale": 1,
        "env.reward_cash_scale": 0,
        "env.reward_potential_scale": 0,
        "env.reward_progress_terminal_money_scale": 0,
        "env.reward_progress_maintenance_scale": 0,
        "env.reward_potential_gamma": 0.9997,
        "train.total_timesteps": args.steps,
        "train.seed": args.seed,
        "train.minibatch_size": args.minibatch_size,
        "train.horizon": args.horizon,
        "train.gamma": 0.9997,
        "train.gae_lambda": 0.999,
        "train.reward_clip": 0,
        "train.anneal_lr": 0,
        "train.emag_kl_coef": 0,
    }
    if args.league is not None:
        overrides.update({
            "vec.num_frozen_banks": args.league_banks,
            "vec.frozen_bank_hidden_size": args.hidden_size,
            "vec.frozen_bank_num_layers": args.layers,
            "vec.frozen_bank_pct": args.league_pct,
            "selfplay.enabled": 1,
            "selfplay.opponent_league": str(args.league),
            "selfplay.opponent_pool_prob": 1,
            # Do not overwrite frozen-policy seats with native bots. The league
            # itself should contain any specialists desired for this phase.
            "env.bot_opponent_fraction": 0,
        })
    return overrides


def evaluation_overrides(args: argparse.Namespace, checkpoint: Path) -> dict[str, Any]:
    return {
        "base.load_model_path": str(checkpoint),
        "base.num_games": args.eval_games,
        "base.eval_agents": min(max(16, args.eval_games * 2), 128),
        "base.eval_deterministic": int(args.deterministic),
        "base.seed": args.seed,
        "policy.hidden_size": args.hidden_size,
        "policy.num_layers": args.layers,
        "vec.num_frozen_banks": 0,
        "vec.frozen_bank_pct": 0,
        "selfplay.enabled": 0,
        "env.macro_mode": 2,
        "env.episode_steps": 720,
        "env.seed": args.seed,
        "env.reset_state_prob": 0,
        "env.bot_opponent_fraction": 1,
        "env.bot_pass_fraction": 0,
        "env.bot_top_fraction": 0.35,
        "env.bot_rules_fraction": 0.20,
        "env.bot_script_fraction": 0.20,
        "env.bot_adaptive_fraction": 0.25,
    }


def _select_checkpoints(run_dir: Path, maximum: int) -> list[Path]:
    checkpoints = sorted(path for path in run_dir.glob("*.bin")
                         if path.is_file() and path.stat().st_size > 0)
    if len(checkpoints) <= maximum:
        return checkpoints
    positions = {round(index * (len(checkpoints) - 1) / (maximum - 1))
                 for index in range(maximum)} if maximum > 1 else {len(checkpoints) - 1}
    return [checkpoints[index] for index in sorted(positions)]


def _run_process(command: list[str], log: Path, env: dict[str, str], dry_run: bool) -> str:
    if dry_run:
        log.write_text(" ".join(command) + "\n")
        return ""
    result = subprocess.run(command, cwd=REPO, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_text(result.stdout)
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}); see {log}")
    return result.stdout


def run_trial(args: argparse.Namespace, output: Path, trial: int, family: str,
        parameters: dict[str, float | int], gpu: str) -> dict[str, Any]:
    prefix = output.name if output.name.startswith("qd_") else f"qd_{output.name}"
    run_id = f"{prefix}_{trial:04d}_{family}_h{args.hidden_size}x{args.layers}"
    run_dir = REPO / "checkpoints" / "kaggriculture" / run_id
    if run_dir.exists():
        raise FileExistsError(f"refusing to reuse checkpoint directory {run_dir}")
    trial_dir = output / f"trial_{trial:04d}_{family}"
    trial_dir.mkdir(parents=True, exist_ok=False)
    overrides = fixed_training_overrides(args, run_id)
    overrides.update(parameters)
    command = [str(REPO / "puffer"), "train", "kaggriculture"]
    command.extend(_format_override(key, value) for key, value in overrides.items())
    environment = dict(os.environ)
    environment["CUDA_VISIBLE_DEVICES"] = gpu
    _run_process(command, trial_dir / "train.log", environment, args.dry_run)
    if args.dry_run:
        return {"trial": trial, "family": family, "parameters": parameters,
                "run_id": run_id, "records": [], "dry_run": True}

    checkpoints = _select_checkpoints(run_dir, args.checkpoints_per_run)
    if not checkpoints:
        raise RuntimeError(f"training produced no checkpoints in {run_dir}")
    records: list[dict[str, Any]] = []
    for checkpoint in checkpoints:
        eval_command = [str(REPO / "puffer"), "eval", "kaggriculture"]
        eval_command.extend(_format_override(key, value)
                            for key, value in evaluation_overrides(args, checkpoint).items())
        log = trial_dir / f"eval_{checkpoint.stem}.log"
        stdout = _run_process(eval_command, log, environment, False)
        metrics = parse_eval_json(stdout)
        descriptor = behavior_descriptor(metrics)
        money = float(metrics.get("env/money", float("-inf")))
        records.append({
            "trial": trial, "family": family, "run_id": run_id,
            "checkpoint": str(checkpoint.relative_to(REPO)), "parameters": parameters,
            "metrics": metrics, "descriptor": descriptor, "niche": niche_key(descriptor),
            "quality": money, "money": money,
            "win_rate": float(metrics.get("env/win_rate", 0.0)),
        })
    result = {"trial": trial, "family": family, "parameters": parameters,
              "run_id": run_id, "records": records}
    (trial_dir / "result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def _completed_trials(output: Path) -> set[int]:
    completed: set[int] = set()
    path = output / "trials.jsonl"
    if not path.exists():
        return completed
    for line in path.read_text().splitlines():
        try:
            completed.add(int(json.loads(line)["trial"]))
        except (ValueError, KeyError, json.JSONDecodeError):
            continue
    return completed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--trials", type=int, default=18)
    parser.add_argument("--steps", type=int, default=30_000_000)
    parser.add_argument("--eval-games", type=int, default=128)
    parser.add_argument("--checkpoints-per-run", type=int, default=4)
    parser.add_argument("--gpus", default="0", help="comma-separated physical GPU ids")
    parser.add_argument("--league", type=Path,
                        help="optional architecture-compatible league.ini for refinement")
    parser.add_argument("--league-banks", type=int, default=4)
    parser.add_argument("--league-pct", type=float, default=0.75)
    parser.add_argument("--hidden-size", type=int, default=128)
    parser.add_argument("--layers", type=int, default=2)
    parser.add_argument("--agents", type=int, default=2048)
    parser.add_argument("--minibatch-size", type=int, default=2048)
    parser.add_argument("--horizon", type=int, default=64)
    parser.add_argument("--seed", type=int, default=707)
    parser.add_argument("--global-fraction", type=float, default=0.25)
    parser.add_argument("--mutation-strength", type=float, default=0.16)
    parser.add_argument("--deterministic", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.output is None:
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output = REPO / "logs" / "kaggriculture" / f"qd_macro_{stamp}"
    elif not args.output.is_absolute():
        args.output = REPO / args.output
    if args.output.exists() and not args.resume:
        raise FileExistsError(f"output already exists; use --resume: {args.output}")
    args.output.mkdir(parents=True, exist_ok=True)
    if args.league is not None:
        if not args.league.is_absolute():
            args.league = (REPO / args.league).resolve()
        if not args.league.is_file():
            raise FileNotFoundError(f"league manifest not found: {args.league}")
    archive = Archive(args.output / "archive.json")
    completed = _completed_trials(args.output)
    pending = [trial for trial in range(args.trials) if trial not in completed]
    gpus = [gpu.strip() for gpu in args.gpus.split(",") if gpu.strip()]
    if not gpus:
        raise ValueError("--gpus must name at least one GPU")
    print(f"QD archive={args.output} existing_cells={len(archive.entries)} "
          f"pending_trials={len(pending)} gpus={','.join(gpus)}")
    journal = args.output / "trials.jsonl"
    for batch_start in range(0, len(pending), len(gpus)):
        batch_trials = pending[batch_start:batch_start + len(gpus)]
        batch: list[tuple[int, str, dict[str, float | int]]] = []
        # Generate a batch only after the preceding batch updated the archive.
        # Per-trial RNG makes resume stable while still allowing new elites to
        # become mutation parents during a long run.
        for trial in batch_trials:
            rng = random.Random(args.seed + 1_000_003 * trial)
            family = DEFAULT_FAMILIES[trial % len(DEFAULT_FAMILIES)]
            global_sample = (trial < len(DEFAULT_FAMILIES)
                             or rng.random() < args.global_fraction)
            parents = archive.parents()
            parent = rng.choice(parents) if parents and not global_sample else None
            parameters = sample_parameters(
                rng, family, global_sample=global_sample, parent=parent,
                mutation_strength=args.mutation_strength)
            batch.append((trial, family, parameters))
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(batch)) as pool:
            futures = [pool.submit(run_trial, args, args.output, trial, family, params,
                                   gpus[index])
                       for index, (trial, family, params) in enumerate(batch)]
            for future in concurrent.futures.as_completed(futures):
                result = future.result()
                with journal.open("a") as handle:
                    handle.write(json.dumps(result, sort_keys=True) + "\n")
                admitted = 0
                for record in result["records"]:
                    admitted += int(archive.consider(record))
                archive.save()
                best = max((record["money"] for record in result["records"]), default=float("nan"))
                print(f"trial={result['trial']} family={result['family']} "
                      f"checkpoints={len(result['records'])} admitted={admitted} best_money={best:.1f}")
    archive.save()
    print(f"QD complete: cells={len(archive.entries)}/27 archive={archive.path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
