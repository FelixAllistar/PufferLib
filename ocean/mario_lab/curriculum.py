#!/usr/bin/env python3
"""Run bounded curriculum rounds, selecting practice weights from full-course failures."""
import argparse
import datetime as dt
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

from run import ROOT, sha256, timestamp, write_json


def call(command, log, prefix):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False)
    log.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"command exited {result.returncode}; see {log}")
    for line in result.stdout.splitlines():
        if line.startswith(prefix):
            return Path(line[len(prefix):].strip())
    raise RuntimeError(f"command did not report {prefix}; see {log}")


def assess(panel):
    data = json.loads((panel / "manifest.json").read_text())
    if data["status"] != "completed" or set(data["courses"]) != set(map(str, range(1, 8))):
        raise RuntimeError("curriculum requires a complete seven-family evaluation")
    rates = {key: value["summary"]["clear_rate"] for key, value in data["courses"].items()}
    with (panel / data["courses"]["6"]["tape"]).open() as rows:
        episodes = [row for line in rows if (row := json.loads(line))["type"] == "episode"]
    n = len(episodes)
    visits = sum(row["pipe_visits"] > 0 for row in episodes)
    returns = sum(row["pipe_returns"] > 0 for row in episodes)
    weights = {
        "practice_gap": math.ceil(100 * (1 - rates["2"])),
        "practice_entry": math.ceil(100 * (1 - visits / n)),
        "practice_exit": math.ceil(100 * (1 - returns / visits)) if visits else 0,
        "practice_route": math.ceil(100 * (1 - rates["6"])),
    }
    names = ("flat", "gaps", "pipes", "stairs", "walkers", "underground", "composite")
    root_weights = {f"mix_{name}": 10 + math.ceil(90 * (1 - rates[str(i + 1)])) for i, name in enumerate(names)}
    return {"panel": str(panel), "rates": rates, "quality": [min(rates.values()), sum(rates.values()) / 7],
            "weights": weights, "root_weights": root_weights, "pipe_visits": visits, "pipe_returns": returns}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("initial", type=Path, help="completed run.py directory")
    p.add_argument("--rounds", type=int, default=1, help="bounded training rounds; zero evaluates only")
    p.add_argument("--steps", type=int, default=10485760, help="policy decisions per round")
    p.add_argument("--episodes", type=int, default=32, help="full episodes per family and assessment")
    p.add_argument("--agents", type=int, default=128)
    p.add_argument("--length", type=int, default=64)
    p.add_argument("--difficulty", type=int, choices=range(3), default=1)
    p.add_argument("--max-frames", type=int, default=2400)
    p.add_argument("--practice-prob", type=float, default=0.35)
    p.add_argument("--seed", type=int, default=73)
    p.add_argument("--eval-seed", type=int, default=901)
    p.add_argument("--target", type=float, default=1, help="stop if every validation family meets this clear rate")
    p.add_argument("--output", type=Path, default=ROOT / "logs/mario_lab/curricula")
    a = p.parse_args()
    if not 0 <= a.rounds <= 100 or not 1 <= a.episodes <= 100000 or a.steps < a.agents * 64:
        p.error("invalid round, episode or rollout budget")
    if a.agents < 32 or a.agents > 32768 or a.agents & (a.agents - 1):
        p.error("agents must be a power of two in [32, 32768]")
    if not 48 <= a.length <= 192 or not 1 <= a.max_frames <= 100000:
        p.error("unsupported evaluation geometry/frame cap")
    if not 0 <= a.practice_prob <= 1 or not 0 < a.target <= 1:
        p.error("invalid practice probability or target")
    if not 0 <= a.seed <= 2147483547 or not 0 <= a.eval_seed <= 2147483647:
        p.error("unsupported seed")
    initial = a.initial.resolve()
    initial_manifest = json.loads((initial / "manifest.json").read_text())
    if initial_manifest["status"] != "completed" or not initial_manifest.get("checkpoints"):
        p.error("initial run must be complete and contain a checkpoint")
    study_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:8]
    study = a.output.resolve() / study_id; study.mkdir(parents=True, exist_ok=False)
    shutil.copy2(__file__, study / "curriculum.py")
    manifest = {
        "schema": 1, "experiment": "GEN001", "status": "running", "started_utc": timestamp(),
        "initial": str(initial), "requested_rounds": a.rounds, "decisions_per_round": a.steps,
        "practice_prob": a.practice_prob, "source_sha256": sha256(study / "curriculum.py"),
        "protocol": {"length": a.length, "difficulty": a.difficulty, "max_frames": a.max_frames,
                     "episodes_per_family": a.episodes, "seed": a.eval_seed, "split": 1, "practice_prob": 0},
        "selection": "maximize minimum family clear rate, then macro clear rate; validation only",
        "rounds": [],
    }
    path = study / "manifest.json"; write_json(path, manifest)
    print(f"Curriculum: {study}", flush=True)

    def evaluate(run, name):
        command = [sys.executable, str(ROOT / "ocean/mario_lab/evaluate.py"), str(run),
                   "--episodes", str(a.episodes), "--length", str(a.length),
                   "--difficulty", str(a.difficulty), "--max-frames", str(a.max_frames),
                   "--seed", str(a.eval_seed)]
        return assess(call(command, study / f"{name}_eval.log", "Evaluation: "))

    code = 0
    try:
        parent = initial; current = evaluate(parent, "initial")
        best = current; best_run = parent
        manifest.update(initial_assessment=current, best_run=str(best_run), best=best)
        write_json(path, manifest)
        for number in range(1, a.rounds + 1):
            if current["quality"][0] >= a.target:
                manifest["stop_reason"] = "validation target met"; break
            parent_info = json.loads((parent / "manifest.json").read_text())
            checkpoint = parent / parent_info["checkpoints"][-1]["path"]
            arch = parent_info["overrides"]["policy"]
            weights = current["weights"]
            probability = a.practice_prob if sum(weights.values()) else 0
            command = [sys.executable, str(ROOT / "ocean/mario_lab/run.py"), "--steps", str(a.steps),
                       "--agents", str(a.agents), "--hidden", str(arch["hidden_size"]),
                       "--layers", str(arch["num_layers"]), "--course", "0", "--length", str(a.length),
                       "--difficulty", str(a.difficulty), "--max-frames", str(a.max_frames),
                       "--parent", str(checkpoint), "--seed", str(a.seed + number),
                       "--experiment", "GEN001", "--label", f"adaptive-{number:02d}",
                       "--output", str(study / "training"), "--set", f"env.practice_prob={probability}",
                       "--set", "env.practice_frames=800", "--set", "env.practice_short_goals=1"]
            # Adaptive family sampling must not silently switch a calibrated
            # parent back to the legacy controller or shrink its obstacles.
            preserved = ("accel friction walk_speed run_speed jump_speed gravity_hold gravity_release "
                         "max_fall hold_frames completion_reward death_penalty speed_bonus "
                         "physics_mode walk_accel brake_accel player_width player_height "
                         "jump_fast_speed jump_fast_threshold gravity_fast_hold gravity_fast_release "
                         "pipe_min_height pipe_max_height stair_height").split()
            env = parent_info["overrides"]["env"]
            for key in preserved:
                if key in env: command.extend(["--set", f"env.{key}={env[key]}"])
            command.extend(["--progress-reward", str(env.get("progress_reward", 0.001))])
            for key in ("learning_rate", "ent_coef", "gamma", "gae_lambda"):
                value = parent_info["overrides"]["train"].get(key)
                if value is not None: command.extend(["--set", f"train.{key}={value}"])
            for key, value in weights.items(): command.extend(["--set", f"env.{key}={value}"])
            for key, value in current["root_weights"].items(): command.extend(["--set", f"env.{key}={value}"])
            record = {"number": number, "parent": str(parent), "assessment_before": current,
                      "command": command, "practice_prob": probability, "weights": weights,
                      "root_weights": current["root_weights"]}
            manifest["rounds"].append(record); write_json(path, manifest)
            print(f"Round {number}: practice probability {probability}; weights {weights}", flush=True)
            parent = call(command, study / f"round_{number}_train.log", "Run: ")
            record["run"] = str(parent); write_json(path, manifest)
            current = evaluate(parent, f"round_{number}"); record["assessment_after"] = current
            if current["quality"] > best["quality"]: best = current; best_run = parent
            manifest.update(best_run=str(best_run), best=best); write_json(path, manifest)
            print(f"Round {number}: minimum clear rate {current['quality'][0]:.3f}; "
                  f"macro clear rate {current['quality'][1]:.3f}", flush=True)
        manifest["status"] = "completed"
        manifest.setdefault("stop_reason", "round budget exhausted")
    except KeyboardInterrupt:
        manifest["status"] = "interrupted"; code = 130
    except Exception as error:
        manifest.update(status="failed", error=str(error)); code = 1
    finally:
        manifest["finished_utc"] = timestamp(); write_json(path, manifest)
    print(f"{manifest['status']}: {path}\nSelected run: {manifest.get('best_run', initial)}", flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
