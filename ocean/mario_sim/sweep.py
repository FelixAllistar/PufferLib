#!/usr/bin/env python3
"""Launch a native PROTEIN search; score every trial on the same complete ROM episodes.

The native parent invokes this file with `train --section.key=value ...` as its
worker. Training exits before evaluation, releasing its GPU allocations. The
worker owns the result pipe and reports only verified fixed-panel FPG success.
"""
import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import resource
import signal
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ENV = "mario_sim"
RESULT = struct.Struct("=fffi192f")  # src/pufferl.cu TrainResult, 784 bytes


def read_config(profile):
    config = configparser.ConfigParser(interpolation=None)
    for path in (ROOT / "config/default.ini", ROOT / f"config/{ENV}.ini", profile):
        with path.open() as stream:
            config.read_file(stream)
    return config


def flattened(config):
    return {f"{section}.{key}": value.strip("'\"")
            for section in config.sections() for key, value in config[section].items()}


def worker_arguments(argv):
    values = {}
    for arg in argv:
        if not arg.startswith("--") or "=" not in arg:
            raise ValueError(f"invalid native argument: {arg}")
        key, value = arg[2:].split("=", 1)
        values[key] = value
    return values


def run_logged(command, path, process_group=False):
    with path.open("x") as stream:
        print(json.dumps(command), file=stream, flush=True)
        process = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=process_group)
        try:
            code = process.wait()
        except BaseException:
            if process.poll() is None:
                if process_group:
                    os.killpg(process.pid, signal.SIGTERM)
                else:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    if process_group:
                        os.killpg(process.pid, signal.SIGKILL)
                    else:
                        process.kill()
                    process.wait()
            raise
    if code:
        raise RuntimeError(f"command exited {code}; see {path}")


def panel_score(reports, depths, episodes, seed, frame_cap):
    if len(reports) != len(depths) or not reports:
        raise ValueError("incomplete evaluation panel")
    wins = 0
    for report, depth in zip(reports, depths):
        expected = dict(passed=True, mismatches=0, objective="fpg", reference_frames=depth,
                        episodes_requested=episodes, episodes_run=episodes,
                        episodes_completed=episodes, seed=seed, max_frames=frame_cap,
                        deterministic=0)
        if any(report.get(k) != v for k, v in expected.items()):
            raise ValueError(f"invalid evaluation at depth {depth}: {report}")
        n = report["rom_successes"]
        if not isinstance(n, int) or not 0 <= n <= episodes or n != report["native_successes"]:
            raise ValueError("invalid or unequal simulator/ROM win counts")
        wins += n
    return wins / (len(depths) * episodes)


def result_packet(score, seconds, steps):
    if not (math.isfinite(score) and 0 <= score <= 1 and math.isfinite(seconds)
            and seconds > 0 and steps > 0):
        raise ValueError("invalid sweep result")
    return RESULT.pack(score, seconds, steps, 1, score, *([0.0] * 63),
                       seconds, *([0.0] * 63), steps, *([0.0] * 63))


def run_trial(values):
    fd = int(values["base.result_fd"])
    if fd <= 2:
        raise ValueError("worker requires the native sweep result pipe")
    os.set_inheritable(fd, False)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run_id = values["base.run_id"]
    if not re.fullmatch(r"sweep_[0-9]+_[0-9]+", run_id):
        raise ValueError("invalid native run ID")
    directory = ROOT / values["base.log_dir"] / ENV / run_id
    directory.mkdir(parents=True, exist_ok=False)
    receipt = dict(status="running", parameters=values, started=time.time())
    (directory / "inputs.json").write_text(json.dumps(receipt, indent=2) + "\n")
    started = time.monotonic()
    try:
        if values["env.mode"] != "fpg" or values["base.load_model_path"] != "None" or float(values["fpg.curriculum_resume"]) != 0:
            raise ValueError("architecture sweep requires fresh weights and curriculum")
        batch = int(float(values["vec.total_agents"])) * int(float(values["train.horizon"]))
        steps = int(float(values["train.total_timesteps"]))
        if steps < batch or steps % batch:
            raise ValueError("equal trial budget must contain complete rollouts")
        checkpoint = ROOT / values["base.checkpoint_dir"] / ENV / run_id / f"{steps:016d}.bin"
        if checkpoint.parent.exists():
            raise ValueError(f"checkpoint directory already exists: {checkpoint.parent}")
        train_values = values | {"base.result_fd": "0", "base.eval_episodes": "0"}
        command = [str(ROOT / values["sweep.trainer_path"]), "train"]
        command += [f"--{key}={value}" for key, value in train_values.items()]
        run_logged(command, directory / "train.log")
        if not checkpoint.is_file() or not checkpoint.stat().st_size:
            raise ValueError("trainer did not save its final checkpoint")
        # A standalone, fixed evaluation config; never use the trial's reward,
        # frontier, promotion rules, episode limit, or saved mastery state.
        frame_cap = int(float(values["sweep.eval_max_frames"]))
        episodes = int(float(values["sweep.eval_episodes"]))
        seed = int(float(values["sweep.eval_seed"]))
        depths = [int(x) for x in values["sweep.eval_depths"].split(",")]
        evaluation = configparser.ConfigParser(interpolation=None)
        evaluation["policy"] = {k: values[f"policy.{k}"] for k in ("hidden_size", "num_layers")}
        evaluation["env"] = {k: values[f"env.{k}"] for k in
                             ("engine_cpu_archive", "engine_module")}
        evaluation["fpg"] = {"time_table": values["fpg.time_table"], "max_frames": str(frame_cap)}
        eval_config = directory / "evaluation.ini"
        with eval_config.open("x") as stream:
            evaluation.write(stream)
        reports = []
        for depth in depths:
            output = directory / f"depth_{depth}"
            command = [str(ROOT / values["sweep.evaluator_path"]), str(checkpoint),
                       str(eval_config), str(output), str(episodes), str(seed), "0",
                       str(ROOT / values["sweep.eval_bank"]), str(depth), "fpg"]
            run_logged(command, directory / f"eval_{depth}.log")
            reports.append(json.loads((output / "summary.json").read_text()))
        score = panel_score(reports, depths, episodes, seed, frame_cap)
        seconds = time.monotonic() - started
        receipt.update(status="complete", score=score, objective="fixed_panel_fpg_rate",
                       seconds=seconds, steps=steps, checkpoint=str(checkpoint),
                       checkpoint_sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
                       evaluations=reports)
        (directory / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        packet = result_packet(score, seconds, steps)
        if os.write(fd, packet) != len(packet):
            raise OSError("short write to sweep result pipe")
    except BaseException as error:
        receipt.update(status="failed", error=str(error), seconds=time.monotonic() - started)
        (directory / "failure.json").write_text(json.dumps(receipt, indent=2) + "\n")
        raise
    finally:
        os.close(fd)


def show_results(directory):
    results = [json.loads(path.read_text()) for path in
               directory.glob(f"logs/{ENV}/sweep_*/result.json")]
    results.sort(key=lambda r: (-r["score"], r["seconds"]))
    print(f"{len(results)} completed trials; fixed-panel FPG rate (higher is better)")
    for result in results[:10]:
        print(f"{result['score']:.4f}  {result['seconds'] / 60:.1f} min  {result['checkpoint']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, default=HERE / "profiles/sweep.ini")
    parser.add_argument("--runs", type=int)
    parser.add_argument("--steps", type=int)
    parser.add_argument("--eval-episodes", type=int)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summary", type=Path, help="rank completed trials in this sweep directory")
    args = parser.parse_args()
    if args.summary:
        show_results(args.summary.resolve())
        return
    config = read_config(args.profile.resolve())
    if args.runs is not None:
        config["sweep"]["max_runs"] = str(args.runs)
    if args.steps is not None:
        config["train"]["total_timesteps"] = str(args.steps)
        config["sweep.train.total_timesteps"]["min"] = str(args.steps)
        config["sweep.train.total_timesteps"]["max"] = str(args.steps)
    if args.eval_episodes is not None:
        config["sweep"]["eval_episodes"] = str(args.eval_episodes)
    steps = config.getint("train", "total_timesteps")
    max_batch = int(float(config["sweep.vec.total_agents"]["max"])) * int(float(config["sweep.train.horizon"]["max"]))
    if steps < max_batch or steps % max_batch:
        parser.error(f"--steps must be a positive multiple of the largest rollout ({max_batch})")
    if config.getint("sweep", "max_runs") < 1 or config.getint("sweep", "eval_episodes") < 1:
        parser.error("runs and evaluation episodes must be positive")
    config["sweep"]["worker_path"] = str(Path(__file__).resolve())
    # Never inherit the normal training continuation, including after local edits.
    config["base"]["load_model_path"] = "None"
    config["fpg"]["curriculum_resume"] = "0"
    config["env"]["mode"] = "fpg"
    values = flattened(config)
    ranges = {}
    for section in config.sections():
        if not section.startswith("sweep."):
            continue
        low, high = (float(config[section][key]) for key in ("min", "max"))
        baseline = float(values[section[6:]])
        if not low <= baseline <= high:
            parser.error(f"baseline outside {section} range: {baseline} not in [{low}, {high}]")
        if low != high:
            ranges[section[6:]] = [low, high, config[section]["distribution"]]
    if args.dry_run:
        print(json.dumps(dict(runs=config.getint("sweep", "max_runs"), steps_per_trial=steps,
                              total_steps=steps * config.getint("sweep", "max_runs"),
                              fresh_weights=True, evaluation=dict(config["sweep"]),
                              varying_parameters=ranges), indent=2))
        return
    for key in ("trainer_path", "evaluator_path"):
        if not os.access(ROOT / config["sweep"][key], os.X_OK):
            parser.error(f"build executable {config['sweep'][key]} first; see README.md")
    output = args.output.resolve() if args.output else ROOT / "reports" / ENV / datetime.now(timezone.utc).strftime("sweep_%Y%m%d_%H%M%S")
    output.mkdir(parents=True, exist_ok=False)
    config["base"]["log_dir"] = str(output / "logs")
    config["base"]["checkpoint_dir"] = str(output / "checkpoints")
    with (output / "config.ini").open("x") as stream:
        config.write(stream)
    command = [str(ROOT / config["sweep"]["trainer_path"]), "sweep"]
    command += [f"--{key}={value}" for key, value in flattened(config).items()]
    print(f"Starting {config['sweep']['max_runs']} trials x {steps:,} frames; results: {output}", flush=True)
    print(f"Progress: tail -f {output / 'sweep.log'}", flush=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run_logged(command, output / "sweep.log", process_group=True)
    show_results(output)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "train":
        run_trial(worker_arguments(sys.argv[2:]))
    else:
        main()
