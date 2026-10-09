#!/usr/bin/env python3
"""Optional legacy short-course experiment harness; normal training reads config/mario_lab.ini directly."""
import argparse
import configparser
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = ROOT / "ocean/mario_lab/presets/legacy_baseline.ini"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def timestamp():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def write_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def capture(args):
    try:
        result = subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=10, check=False)
        return result.stdout.strip()
    except (OSError, subprocess.TimeoutExpired) as error:
        return str(error)


def arguments():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--steps", type=int, required=True, help="native frame/decision budget")
    p.add_argument("--binary", type=Path, default=ROOT / "build/mario_lab/puffer")
    p.add_argument("--output", type=Path, default=ROOT / "logs/mario_lab/experiments")
    p.add_argument("--experiment", default="CUDA001")
    p.add_argument("--label", default="baseline")
    p.add_argument("--agents", type=int, default=1024)
    p.add_argument("--hidden", type=int, default=128)
    p.add_argument("--layers", type=int, default=2)
    p.add_argument("--seed", type=int, default=73)
    p.add_argument("--course", type=int, choices=range(8), default=0)
    p.add_argument("--difficulty", type=int, choices=range(3), default=1)
    p.add_argument("--length", type=int, default=96)
    p.add_argument("--max-frames", type=int, default=2400)
    p.add_argument("--require-pipe", action="store_true")
    p.add_argument("--progress-reward", type=float, default=0.001)
    p.add_argument("--profile", type=Path, help="JSON environment profile; explicit --set values override it")
    p.add_argument("--set", dest="settings", action="append", default=[], metavar="SECTION.KEY=VALUE",
                   help="numeric learner, reward or controller override; repeat for ablations")
    p.add_argument("--parent", type=Path, help="load weights; this does not restore optimizer/RNG state")
    p.add_argument("--eval-episodes", type=int, default=0,
                   help="optional native validation smoke after checkpoint reload; vector batches may overshoot")
    p.add_argument("--dry-run", action="store_true", help="save the concrete run without launching it")
    a = p.parse_args()
    if a.agents < 32 or a.agents > 32768 or a.agents & (a.agents - 1):
        p.error("--agents must be a power of two in [32, 32768]")
    if a.hidden not in (32, 64, 128, 256, 512) or not 1 <= a.layers <= 8:
        p.error("--hidden must be 32/64/128/256/512; --layers must be in [1, 8]")
    if a.steps < a.agents * 64:
        p.error("--steps must cover at least one agents*64 rollout")
    if not 0 <= a.seed <= 2147483647 or not 48 <= a.length <= 192 or not 1 <= a.max_frames <= 100000:
        p.error("seed, length or frame cap outside supported bounds")
    if not math.isfinite(a.progress_reward) or not 0 <= a.progress_reward <= 100 or a.eval_episodes < 0:
        p.error("invalid reward coefficient or evaluation episode count")
    for field in (a.experiment, a.label):
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,48}", field):
            p.error("experiment and label must be 1–48 letters, digits, underscores or hyphens")
    a.binary = a.binary.resolve()
    if not a.binary.is_file() or not os.access(a.binary, os.X_OK):
        p.error(f"missing executable {a.binary}; build it first")
    if a.parent:
        a.parent = a.parent.resolve()
        if not a.parent.is_file():
            p.error(f"missing parent checkpoint {a.parent}")
        expected = 4 * (1128 * a.hidden + 65 * a.hidden + a.layers * 3 * a.hidden * a.hidden)
        if a.parent.stat().st_size != expected:
            p.error("parent size does not match the selected Mario Lab MLP architecture; check --hidden and --layers")
    defaults = configparser.ConfigParser(interpolation=None)
    defaults.read([ROOT / "config/default.ini", TEMPLATE])
    train_keys = set(defaults["train"]) - {"gpus", "horizon", "minibatch_size", "total_timesteps"}
    env_keys = set("accel friction walk_speed run_speed jump_speed gravity_hold gravity_release "
                   "max_fall hold_frames completion_reward death_penalty speed_bonus practice_prob "
                   "practice_gap practice_entry practice_exit practice_route practice_frames practice_short_goals "
                   "mix_flat mix_gaps mix_pipes mix_stairs mix_walkers mix_underground mix_composite "
                   "physics_mode walk_accel brake_accel player_width player_height jump_fast_speed jump_fast_threshold "
                   "gravity_fast_hold gravity_fast_release pipe_min_height pipe_max_height stair_height".split())
    a.extra = {}
    if a.profile:
        a.profile = a.profile.resolve()
        try:
            profile = json.loads(a.profile.read_text())
            values = profile["env"]
            if not isinstance(values, dict) or not values:
                raise ValueError("profile requires a nonempty env object")
            for key, value in values.items():
                if key not in env_keys or isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value):
                    raise ValueError(f"unsupported profile setting env.{key}")
            a.extra["env"] = {key: str(value) for key, value in values.items()}
        except (OSError, ValueError, KeyError, TypeError) as error:
            p.error(f"invalid profile: {error}")
    for setting in a.settings:
        match = re.fullmatch(r"(train|env)\.([a-z_]+)=(.+)", setting)
        if not match:
            p.error("--set expects train.KEY=NUMBER or env.KEY=NUMBER")
        section, key, value = match.groups()
        if key not in (train_keys if section == "train" else env_keys):
            p.error(f"unsupported --set key {section}.{key}; use a dedicated flag for geometry/budget/architecture")
        try:
            valid = math.isfinite(float(value))
        except ValueError:
            valid = False
        if not valid:
            p.error(f"--set value must be finite and numeric: {setting}")
        a.extra.setdefault(section, {})[key] = value
    return a


def main():
    a = arguments()
    run_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:8]
    run = a.output.resolve() / f"{a.experiment}_{a.label}_{run_id}"
    run.mkdir(parents=True, exist_ok=False)
    source_files = [ROOT / "build.sh", ROOT / "config/default.ini", ROOT / "config/mario_lab.ini", TEMPLATE,
                    ROOT / "ocean/retro/EXPERIMENTS.md"]
    for directory in (ROOT / "src", ROOT / "ocean/mario_lab"):
        source_files.extend(p for p in directory.rglob("*") if p.is_file()
                            and (p.suffix in (".h", ".c", ".cpp", ".cu", ".py", ".md") or p.name == "Makefile"))
    hashes = {}
    for source in sorted(set(source_files)):
        relative = source.relative_to(ROOT)
        dest = run / "source" / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
        hashes[str(relative)] = sha256(dest)
    (run / "bin").mkdir()
    binary = run / "bin/puffer"
    shutil.copy2(a.binary, binary)
    parent = None
    if a.parent:
        parent = run / "parent.bin"
        shutil.copy2(a.parent, parent)
        sidecar = a.parent.with_suffix(a.parent.suffix + ".ini")
        if sidecar.is_file():
            shutil.copy2(sidecar, run / "parent.bin.ini")
    (run / "config").mkdir()
    if a.profile: shutil.copy2(a.profile, run / "profile.json")
    shutil.copy2(run / "source/config/default.ini", run / "config/default.ini")
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(run / "source" / TEMPLATE.relative_to(ROOT))
    overrides = {
        "base": {"run_id": run_id, "seed": a.seed, "checkpoint_dir": str(run / "checkpoints"),
                 "log_dir": str(run / "metrics"), "load_model_path": str(parent) if parent else "None",
                 "eval_episodes": 0},
        "vec": {"total_agents": a.agents},
        "policy": {"hidden_size": a.hidden, "num_layers": a.layers},
        "train": {"total_timesteps": a.steps, "horizon": 64, "minibatch_size": min(4096, a.agents * 64)},
        "env": {"seed": a.seed, "split": 0, "course": a.course, "difficulty": a.difficulty,
                "length": a.length, "max_frames": a.max_frames, "require_pipe": int(a.require_pipe),
                "progress_reward": a.progress_reward},
    }
    for section, values in a.extra.items():
        overrides[section].update(values)
    for section, values in overrides.items():
        for key, value in values.items():
            cfg[section][key] = str(value)
    with (run / "config/mario_lab.ini").open("w") as stream:
        cfg.write(stream)
    command = [str(binary), "train"]
    manifest = {
        "schema": 1, "experiment": a.experiment, "label": a.label, "run_id": run_id,
        "status": "prepared", "created_utc": timestamp(), "cwd": str(run), "command": command,
        "requested_decisions": a.steps, "scheduled_decisions": a.steps // (a.agents * 64) * a.agents * 64,
        "frames_per_decision": 1, "overrides": overrides, "source_sha256": hashes,
        "binary": {"original": str(a.binary), "snapshot": str(binary), "sha256": sha256(binary)},
        "build_provenance": "binary captured; source snapshot is not proof this binary was built from it",
        "parent": {"original": str(a.parent), "sha256": sha256(parent)} if parent else None,
        "resume_kind": "weights_only" if parent else "fresh",
        "profile": {"original": str(a.profile), "snapshot": "profile.json", "sha256": sha256(run / "profile.json")} if a.profile else None,
        "git_head": capture(["git", "rev-parse", "HEAD"]),
        "git_status": capture(["git", "status", "--short"]),
        "platform": platform.platform(), "python": sys.version,
        "gpu": capture(["nvidia-smi", "--query-gpu=name,driver_version,memory.total", "--format=csv,noheader"]),
        "config_sha256": {p.name: sha256(p) for p in (run / "config").glob("*.ini")},
    }
    manifest_path = run / "manifest.json"
    write_json(manifest_path, manifest)
    print(f"Run: {run}\nManifest: {manifest_path}", flush=True)
    if a.dry_run:
        return 0
    manifest.update(status="running", started_utc=timestamp())
    write_json(manifest_path, manifest)
    started = time.monotonic()
    code = 1
    try:
        with (run / "console.log").open("w") as log:
            code = subprocess.call(command, cwd=run, stdout=log, stderr=subprocess.STDOUT)
        manifest["train_exit_code"] = code
        manifest["train_wall_seconds"] = time.monotonic() - started
        checkpoints = sorted((run / "checkpoints").rglob("*.bin"))
        manifest["checkpoints"] = [{"path": str(p.relative_to(run)), "sha256": sha256(p), "bytes": p.stat().st_size}
                                   for p in checkpoints]
        if code == 0 and not checkpoints:
            raise RuntimeError("trainer exited successfully without a checkpoint")
        if code == 0 and a.eval_episodes:
            evaluation = [str(binary), "eval", str(checkpoints[-1]), "--headless",
                          f"--base.eval_episodes={a.eval_episodes}", "--env.split=1", "--env.practice_prob=0"]
            manifest["validation_smoke"] = {
                "command": evaluation, "split": 1,
                "sampling": "native stochastic vector evaluation; may overshoot/censor episodes, not a fixed-seed quality panel",
            }
            write_json(manifest_path, manifest)
            with (run / "validation.log").open("w") as log:
                code = subprocess.call(evaluation, cwd=run, stdout=log, stderr=subprocess.STDOUT)
            manifest["validation_smoke"]["exit_code"] = code
            lines = (run / "validation.log").read_text().splitlines()
            manifest["validation_smoke"]["result"] = next((v for v in reversed(lines) if v.startswith("CUDA_EVAL ")), None)
            if code == 0 and manifest["validation_smoke"]["result"] is None:
                raise RuntimeError("validation exited without a CUDA_EVAL result")
        manifest["status"] = "completed" if code == 0 else "failed"
    except KeyboardInterrupt:
        manifest["status"] = "interrupted"
        code = 130
    except Exception as error:
        manifest.update(status="failed", error=str(error))
        code = 1
    finally:
        manifest.update(finished_utc=timestamp(), total_wall_seconds=time.monotonic() - started)
        write_json(manifest_path, manifest)
    print(f"{manifest['status']}: {manifest_path}", flush=True)
    for name in ("console.log", "validation.log"):
        log = run / name
        if log.exists():
            print(f"{name} tail:")
            print("\n".join(log.read_text(errors="replace").splitlines()[-14:]))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
