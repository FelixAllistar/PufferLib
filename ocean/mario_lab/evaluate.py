#!/usr/bin/env python3
"""Evaluate a saved Mario Lab run on complete seeded episodes per course family."""
import argparse
import configparser
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

from run import ROOT, sha256, timestamp, write_json


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("run", type=Path, help="directory containing a run.py manifest")
    p.add_argument("--binary", type=Path, default=ROOT / "build/mario_lab/eval_policy")
    p.add_argument("--episodes", type=int, default=32, help="completed episodes per family")
    p.add_argument("--courses", type=int, nargs="+", choices=range(8), default=list(range(1, 8)))
    p.add_argument("--split", type=int, choices=(1, 2), default=1)
    p.add_argument("--seed", type=int, default=901)
    p.add_argument("--length", type=int, help="evaluation course length; default uses training config")
    p.add_argument("--difficulty", type=int, choices=range(3), help="evaluation difficulty")
    p.add_argument("--max-frames", type=int, help="evaluation frame cap")
    a = p.parse_args()
    if not 1 <= a.episodes <= 100000 or not 0 <= a.seed <= 2147483647:
        p.error("invalid episode count or seed")
    if a.length is not None and not 48 <= a.length <= 192:
        p.error("length outside supported range")
    if a.max_frames is not None and not 1 <= a.max_frames <= 100000:
        p.error("frame cap outside supported range")
    a.run = a.run.resolve(); a.binary = a.binary.resolve()
    training = json.loads((a.run / "manifest.json").read_text())
    checkpoints = training.get("checkpoints", [])
    if not checkpoints or not a.binary.is_file():
        p.error("a checkpoint and the policy-eval binary are required")
    checkpoint = a.run / checkpoints[-1]["path"]
    if sha256(checkpoint) != checkpoints[-1]["sha256"]:
        p.error("checkpoint hash differs from its training manifest")
    panel_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:8]
    panel = a.run / "evaluations" / panel_id
    panel.mkdir(parents=True, exist_ok=False)
    shutil.copytree(a.run / "config", panel / "config")
    # Old run configs predate this key. Native CLI overrides require the key
    # to exist, and every panel must evaluate the ordinary root-start task.
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(panel / "config/mario_lab.ini")
    cfg["env"]["practice_prob"] = "0"
    with (panel / "config/mario_lab.ini").open("w") as stream:
        cfg.write(stream)
    shutil.copy2(checkpoint, panel / "checkpoint.bin")
    shutil.copy2(a.binary, panel / "eval_policy")
    sources = {}
    for name in ("ml_sim.h", "ml_config.h", "mario_lab.h", "eval.c", "evaluate.py", "run.py", "Makefile"):
        src = ROOT / "ocean/mario_lab" / name
        dest = panel / "source/ocean/mario_lab" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest); sources[f"ocean/mario_lab/{name}"] = sha256(dest)
    for src in (ROOT / "src").rglob("*"):
        if not src.is_file() or src.suffix not in (".h", ".c", ".cu"):
            continue
        dest = panel / "source" / src.relative_to(ROOT)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest); sources[str(src.relative_to(ROOT))] = sha256(dest)
    manifest = {
        "schema": 1, "status": "running", "started_utc": timestamp(),
        "training_run": str(a.run), "checkpoint_sha256": sha256(panel / "checkpoint.bin"),
        "binary_sha256": sha256(panel / "eval_policy"), "source_sha256": sources,
        "core_matches_training_snapshot": sources["ocean/mario_lab/ml_sim.h"] == training["source_sha256"]["ocean/mario_lab/ml_sim.h"],
        "build_provenance": "binary captured; source snapshot is not proof this binary was built from it",
        "config_sha256": {q.name: sha256(q) for q in (panel / "config").glob("*.ini")},
        "seed": a.seed, "split": a.split, "episodes_per_family": a.episodes,
        "evaluation_overrides": {"practice_prob": 0, "length": a.length,
                                 "difficulty": a.difficulty, "max_frames": a.max_frames},
        "sampling": "complete episodes; stochastic policy; per-episode action RNG; recurrent state reset",
        "courses": {},
    }
    path = panel / "manifest.json"; write_json(path, manifest)
    print(f"Evaluation: {panel}", flush=True)
    start = time.monotonic(); code = 0
    try:
        for course in sorted(set(a.courses)):
            command = [str(panel / "eval_policy"), str(panel / "checkpoint.bin"),
                       f"--base.eval_episodes={a.episodes}", f"--env.course={course}",
                       f"--env.split={a.split}", f"--env.seed={a.seed}", "--env.practice_prob=0"]
            for key, value in (("length", a.length), ("difficulty", a.difficulty), ("max_frames", a.max_frames)):
                if value is not None: command.append(f"--env.{key}={value}")
            record = {"command": command, "tape": f"course_{course}.jsonl"}
            manifest["courses"][str(course)] = record
            with (panel / record["tape"]).open("w") as output, (panel / f"course_{course}.stderr").open("w") as error:
                result = subprocess.run(command, cwd=panel, stdout=output, stderr=error, check=False)
            record["exit_code"] = result.returncode
            if result.returncode:
                raise RuntimeError(f"course {course} failed with exit code {result.returncode}")
            with (panel / record["tape"]).open() as rows:
                last = ""
                for line in rows:
                    last = line
            summary = json.loads(last)
            if summary.get("type") != "summary" or summary["episodes"] != a.episodes:
                raise RuntimeError("incomplete episode panel")
            record["summary"] = summary
            record["tape_sha256"] = sha256(panel / record["tape"])
            write_json(path, manifest)
            print(f"course {course}: {summary['clears']}/{a.episodes} clears, "
                  f"{summary['mean_clear_frames']:.1f} mean clear frames", flush=True)
        manifest["status"] = "completed"
    except KeyboardInterrupt:
        manifest["status"] = "interrupted"; code = 130
    except Exception as error:
        manifest.update(status="failed", error=str(error)); code = 1
    finally:
        manifest.update(finished_utc=timestamp(), wall_seconds=time.monotonic() - start)
        write_json(path, manifest)
    print(f"{manifest['status']}: {path}", flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
