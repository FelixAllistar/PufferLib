#!/usr/bin/env python3
"""Measure ROM mechanics or evaluate a frozen synthetic policy in natural SMB1 1-1."""
import argparse
import configparser
import datetime as dt
import json
import math
from pathlib import Path
import shutil
import subprocess
import time
import uuid

from run import ROOT, sha256, timestamp, write_json

CASES = ("idle", "run", "walk", "brake", "reverse", "jump_hold", "jump_tap", "run_jump", "down_run", "up_run", "both_run")


def rows(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def copy_artifact(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    return sha256(target)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("evaluate", "calibrate"))
    p.add_argument("--run", type=Path, help="completed run.py training directory")
    p.add_argument("--profile", type=Path, default=ROOT / "ocean/mario_lab/profiles/ntsc_small.json",
                   help="profile to compare in calibration; evaluation uses the training config")
    p.add_argument("--binary", type=Path, default=ROOT / "build/mario_lab/rom_bridge")
    p.add_argument("--sim-probe", type=Path, default=ROOT / "build/mario_lab/sim_probe")
    p.add_argument("--output", type=Path, default=ROOT / "logs/mario_lab/transfers")
    p.add_argument("--label", default="probe")
    p.add_argument("--episodes", type=int, default=32)
    p.add_argument("--max-frames", type=int, default=4000)
    p.add_argument("--seed", type=int, default=901)
    p.add_argument("--traces", type=int, default=4)
    p.add_argument("--deterministic", action="store_true")
    a = p.parse_args()
    if not 1 <= a.episodes <= 10000 or not 100 <= a.max_frames <= 100000 or not 0 <= a.traces <= a.episodes:
        p.error("episode, frame or trace budget outside supported bounds")
    if not 0 <= a.seed <= 2147483647 or not a.label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in a.label):
        p.error("invalid seed or label")
    a.binary = a.binary.resolve()
    rom = ROOT / "ocean/retro/roms/smb1_ntsc.nes"
    if not a.binary.is_file() or not rom.is_file():
        p.error("build rom-bridge and provide the locally owned, verified SMB1 ROM first")
    training = None
    if a.mode == "evaluate":
        if not a.run: p.error("evaluation requires --run")
        a.run = a.run.resolve()
        training = json.loads((a.run / "manifest.json").read_text())
        if training.get("status") != "completed" or not training.get("checkpoints"):
            p.error("training must have completed and saved a checkpoint")
        model = a.run / training["checkpoints"][-1]["path"]
        if sha256(model) != training["checkpoints"][-1]["sha256"]:
            p.error("checkpoint hash differs from training manifest")
    else:
        a.profile = a.profile.resolve(); a.sim_probe = a.sim_probe.resolve()
        if not a.sim_probe.is_file(): p.error("build sim-probe first")
        profile = json.loads(a.profile.read_text())
        if not isinstance(profile.get("env"), dict): p.error("profile needs an env object")

    ident = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + uuid.uuid4().hex[:8]
    out = a.output.resolve() / f"{a.mode}_{a.label}_{ident}"
    out.mkdir(parents=True, exist_ok=False)
    binary_hash = copy_artifact(a.binary, out / "bin/rom_bridge")
    source_hashes = {}
    for directory in (ROOT / "ocean/mario_lab", ROOT / "ocean/retro", ROOT / "src"):
        for source in sorted(directory.rglob("*")):
            if source.is_file() and (source.suffix in (".h", ".c", ".cpp", ".cc", ".cu", ".py") or source.name == "Makefile"):
                relative = source.relative_to(ROOT)
                source_hashes[str(relative)] = copy_artifact(source, out / "source" / relative)
    manifest = {
        "schema": 1, "status": "running", "mode": a.mode, "label": a.label,
        "started_utc": timestamp(), "binary_sha256": binary_hash, "source_sha256": source_hashes,
        "build_provenance": "binary and source captured; source hashes alone do not establish how the binary was built",
        "rom": {"path": str(rom), "sha256": sha256(rom), "bytes": rom.stat().st_size},
        "protocol": "natural 1-1 playable start; no RAM writes; one frame per decision; empty recurrent state; first death ends attempt",
        "training_on_rom": False,
    }
    command = [str(out / "bin/rom_bridge"), "--mode", a.mode, "--output", str(out)]
    if training:
        manifest["training_run"] = str(a.run)
        manifest["checkpoint_sha256"] = copy_artifact(model, out / "checkpoint.bin")
        manifest["config_sha256"] = copy_artifact(a.run / "config/mario_lab.ini", out / "config/mario_lab.ini")
        command += ["--model", str(out / "checkpoint.bin"), "--config", str(out / "config/mario_lab.ini"),
                    "--episodes", str(a.episodes), "--max-frames", str(a.max_frames),
                    "--seed", str(a.seed), "--trace-episodes", str(a.traces)]
        if a.deterministic: command.append("--deterministic")
        manifest["sampling"] = {"episodes": a.episodes, "seed": a.seed, "deterministic": a.deterministic,
                                "idle_frames": "episode index modulo 32; included in completion time"}
    else:
        manifest["profile_sha256"] = copy_artifact(a.profile, out / "profile.json")
        manifest["sim_probe_sha256"] = copy_artifact(a.sim_probe, out / "bin/sim_probe")
    manifest["command"] = command
    path = out / "manifest.json"; write_json(path, manifest)
    print(f"Transfer: {out}", flush=True)
    start = time.monotonic(); code = 0
    try:
        with (out / "console.log").open("w") as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        if training:
            records = rows(out / "episodes.jsonl")
            summary = records[-1]
            episodes = [r for r in records if r["type"] == "episode"]
            if summary.get("type") != "summary" or len(episodes) != a.episodes or not all(r["replay_verified"] for r in episodes):
                raise RuntimeError("incomplete evaluation or unverified emulator replay")
            manifest["summary"] = summary
            manifest["progress_quantiles"] = {str(q): sorted(r["max_x"] for r in episodes)[int(q * (len(episodes) - 1))]
                                               for q in (0, 0.25, 0.5, 0.75, 1)}
            manifest["unsupported"] = {key: sum(r.get(key, 0) for r in episodes)
                                       for key in ("unknown_enemy_frames", "unknown_cell_frames", "big_player_frames")}
            try:
                from PIL import Image
                for frame in out.glob("*.ppm"):
                    with Image.open(frame) as img: img.save(frame.with_suffix(".png"))
            except ImportError:
                pass
            if a.traces:
                from inspect_transfer import build
                build(out)
            print(f"{summary['clears']}/{a.episodes} real clears; deaths {summary['deaths']}; "
                  f"timeouts {summary['timeouts']}; furthest X {summary['furthest_x']}", flush=True)
        else:
            comparisons = {}
            for name in ("legacy", "measured"):
                cfg = configparser.ConfigParser(interpolation=None)
                cfg.read(ROOT / "config/mario_lab.ini")
                if name == "measured":
                    for key, value in profile["env"].items(): cfg["env"][key] = str(value)
                config = out / f"{name}.ini"
                with config.open("w") as f: cfg.write(f)
                comparisons[name] = {}
                for case in CASES:
                    target = out / f"{name}_{case}.jsonl"
                    with target.open("w") as f:
                        subprocess.run([str(out / "bin/sim_probe"), str(config), case], cwd=ROOT, stdout=f, check=True)
                    real = [r for r in rows(out / f"{case}.jsonl") if r["type"] == "frame"]
                    sim = rows(target)
                    if len(real) != 101 or len(sim) != 101 or any(r["tick"] != s["tick"] or r["action"] != s["action"] for r, s in zip(real, sim)):
                        raise RuntimeError("calibration traces are misaligned")
                    errors = {}
                    for key in ("x_fp", "feet_fp"):
                        residuals = [(r[key] - s[key]) / 256 for r, s in zip(real, sim)]
                        errors[key] = {"rms_pixels": math.sqrt(sum(x*x for x in residuals) / len(residuals)),
                                       "max_abs_pixels": max(abs(x) for x in residuals)}
                    comparisons[name][case] = errors
            manifest["calibration"] = comparisons
            write_json(out / "comparison.json", comparisons)
            print(f"Calibration complete; {len(CASES)} aligned reference/controller trajectories per profile", flush=True)
        manifest["status"] = "completed"
    except KeyboardInterrupt:
        manifest["status"] = "interrupted"; code = 130
    except Exception as error:
        manifest.update(status="failed", error=str(error)); code = 1
        print(str(error), flush=True)
    finally:
        manifest.update(finished_utc=timestamp(), wall_seconds=time.monotonic() - start)
        manifest["artifact_sha256"] = {str(f.relative_to(out)): sha256(f) for f in sorted(out.rglob("*"))
                                       if f.is_file() and f != path and "source" not in f.relative_to(out).parts}
        write_json(path, manifest)
    print(f"{manifest['status']}: {path}", flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
