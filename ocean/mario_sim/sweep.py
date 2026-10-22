#!/usr/bin/env python3
"""Launch native Mario PROTEIN sweeps using training progress, without ROM evaluation."""
import argparse
import configparser
from datetime import datetime, timezone
import itertools
import json
import math
import os
from pathlib import Path
import resource
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ENV = "mario_sim"


def read_config(profile):
    config = configparser.ConfigParser(interpolation=None)
    for path in (ROOT / "config/default.ini", ROOT / f"config/{ENV}.ini", profile):
        with path.open() as stream:
            config.read_file(stream)
    return config


def flattened(config):
    return {f"{section}.{key}": value.strip("'\"")
            for section in config.sections() for key, value in config[section].items()}


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


def completed_steps(requested, agents, horizon):
    batch = agents * horizon
    if requested < batch or min(agents, horizon) < 1:
        raise ValueError("trial budget must contain at least one complete rollout")
    return requested // batch * batch


def integer_choices(config, key):
    section, name = key.split(".", 1)
    bounds = config["sweep." + key] if "sweep." + key in config else None
    if bounds is None:
        return [config.getint(section, name)]
    low, high = (int(float(bounds[k])) for k in ("min", "max"))
    if bounds["distribution"] == "uniform_pow2":
        return [2**n for n in range(30) if low <= 2**n <= high]
    return list(range(low, high + 1))


def validate_search(config):
    values = flattened(config)
    for key, expected in (("base.load_model_path", "None"), ("env.mode", "mixed"),
                          ("sweep.worker_path", "None")):
        if values[key] != expected:
            raise ValueError(f"{key} must be {expected}")
    for key in ("fpg.curriculum_resume", "base.eval_episodes"):
        if float(values[key]) != 0:
            raise ValueError(f"{key} must be disabled for a fresh training-only sweep")
    for key in ("vec.num_buffers", "vec.num_threads", "fpg.fraction"):
        if "sweep." + key in config and any(
                float(config["sweep." + key][k]) != float(values[key]) for k in ("min", "max")):
            raise ValueError(f"keep {key} fixed")
    if float(values["fpg.fraction"]) != 0.25:
        raise ValueError("mixed sweep keeps the FPG frame allocation at 25%")
    steps = config.getint("train", "total_timesteps")
    if any(float(config["sweep.train.total_timesteps"][k]) != steps for k in ("min", "max")):
        raise ValueError("all trials must request the same training budget")
    ranges = {}
    for section in config.sections():
        if not section.startswith("sweep."):
            continue
        low, high = (float(config[section][key]) for key in ("min", "max"))
        baseline = float(values[section[6:]])
        if not all(map(math.isfinite, (low, high, baseline))) or not low <= baseline <= high:
            raise ValueError(f"baseline outside {section} range: {baseline} not in [{low}, {high}]")
        if low != high:
            ranges[section[6:]] = [low, high, config[section]["distribution"]]
    buffers = config.getint("vec", "num_buffers")
    replay = float(config["sweep.train.replay_ratio"]["min"])
    for agents, horizon, minibatch in itertools.product(
            integer_choices(config, "vec.total_agents"), integer_choices(config, "train.horizon"),
            integer_choices(config, "train.minibatch_size")):
        if buffers < 1 or agents % buffers:
            raise ValueError("buffers must divide every allowed actor count")
        if (horizon < 8 or minibatch < horizon or horizon % 8 or minibatch % horizon or
                agents % (minibatch // horizon) or int(replay * agents * horizon / minibatch) < 1):
            raise ValueError("illegal rollout/minibatch shape or zero optimizer updates")
        completed_steps(steps, agents, horizon)
    if config.getint("sweep", "max_runs") < 1 or config.getint("sweep", "max_failures") < 1:
        raise ValueError("sweep.max_runs and sweep.max_failures must be positive")
    return ranges


def last_metric(config, key):
    value = float(config["metrics"][key].split(",")[-1])
    if not math.isfinite(value):
        raise ValueError(f"nonfinite training metric: {key}")
    return value


def results(directory):
    paths = sorted(directory.glob(f"logs/{ENV}/sweep_*.ini"))
    if not paths:
        paths = sorted(directory.glob("sweep_*.ini"))
    rows = []
    for path in paths:
        config = configparser.ConfigParser(interpolation=None)
        config.read(path)
        if not config.has_option("metrics", "env/sweep_score"):
            continue
        rows.append(dict(score=last_metric(config, "env/sweep_score"),
                         game_pixels=last_metric(config, "env/game_progress_pixels"),
                         curriculum_frames=last_metric(config, "env/curriculum_frames"),
                         seconds=last_metric(config, "uptime"),
                         steps=last_metric(config, "agent_steps"), log=str(path)))
    return sorted(rows, key=lambda r: (-r["score"], r["seconds"]))


def show_results(directory):
    rows = results(directory)
    print(f"{len(rows)} completed trials; native training progress score (higher is better)")
    for row in rows[:10]:
        print(f"{row['score']:.4f}  game_pixels={row['game_pixels']:.1f} "
              f"FPG_depth={row['curriculum_frames']:.1f}  "
              f"{row['seconds'] / 60:.1f} min  {row['log']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, default=HERE / "profiles/sweep.ini")
    parser.add_argument("--runs", type=int)
    parser.add_argument("--steps", type=int)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summary", type=Path, help="rank native INI logs from this sweep directory")
    args = parser.parse_args()
    if args.summary:
        show_results(args.summary.resolve())
        return
    config = read_config(args.profile.resolve())
    if args.runs is not None:
        config["sweep"]["max_runs"] = str(args.runs)
    if args.steps is not None:
        config["train"]["total_timesteps"] = str(args.steps)
        for key in ("min", "max"):
            config["sweep.train.total_timesteps"][key] = str(args.steps)
    config["base"]["load_model_path"] = "None"
    config["base"]["eval_episodes"] = "0"
    config["fpg"]["curriculum_resume"] = "0"
    config["env"]["mode"] = "mixed"
    config["sweep"]["worker_path"] = "None"
    try:
        ranges = validate_search(config)
    except ValueError as error:
        parser.error(str(error))
    steps = config.getint("train", "total_timesteps")
    budgets = [completed_steps(steps, n, h) for n, h in itertools.product(
        integer_choices(config, "vec.total_agents"), integer_choices(config, "train.horizon"))]
    if args.dry_run:
        print(json.dumps(dict(runs=config.getint("sweep", "max_runs"), steps_per_trial=steps,
                              completed_steps_range=[min(budgets), max(budgets)],
                              total_steps=steps * config.getint("sweep", "max_runs"),
                              fresh_weights=True, mode=config["env"]["mode"],
                              actors=integer_choices(config, "vec.total_agents"),
                              buffers=config.getint("vec", "num_buffers"),
                              threads=config.getint("vec", "num_threads"),
                              annealing=bool(config.getint("train", "anneal_lr")),
                              metric=config["sweep"]["metric"], rom_evaluation=False,
                              varying_parameters=ranges), indent=2))
        return
    if not os.access(ROOT / config["sweep"]["trainer_path"], os.X_OK):
        parser.error(f"build executable {config['sweep']['trainer_path']} first; see README.md")
    output = args.output.resolve() if args.output else ROOT / "reports" / ENV / datetime.now(timezone.utc).strftime("sweep_%Y%m%d_%H%M%S")
    output.mkdir(parents=True, exist_ok=False)
    config["base"]["log_dir"] = str(output / "logs")
    config["base"]["checkpoint_dir"] = str(output / "checkpoints")
    with (output / "config.ini").open("x") as stream:
        config.write(stream)
    command = [str(ROOT / config["sweep"]["trainer_path"]), "sweep"]
    command += [f"--{key}={value}" for key, value in flattened(config).items()]
    print(f"Starting {config['sweep']['max_runs']} trials x {steps:,} requested frames "
          f"({min(budgets):,}–{max(budgets):,} after rollout rounding); results: {output}", flush=True)
    print(f"Progress: tail -f {output / 'sweep.log'}", flush=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run_logged(command, output / "sweep.log", process_group=True)
    show_results(output)


if __name__ == "__main__":
    main()
