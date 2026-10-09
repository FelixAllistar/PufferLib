#!/usr/bin/env python3
"""Launch a fresh mixed-task PROTEIN sweep, scored on fixed game/FPG ROM panels.

The native parent invokes this file with `train --section.key=value ...` as its
worker. Training exits before evaluation, releasing its GPU allocations. The
worker reports physical game progress/clears and FPG success, not training reward.
"""
import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import itertools
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
GAME_WEIGHT = 0.75


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


def game_panel_score(report, episodes, seed, frame_cap):
    expected = dict(passed=True, mismatches=0, objective="full_game",
                    scope="natural_all_32_stages", reference_frames=0, eligible_starts=32,
                    episodes_requested=episodes, episodes_run=episodes,
                    episodes_completed=episodes, seed=seed, max_frames=frame_cap,
                    deterministic=0, all_episodes_run_to_terminal=True,
                    ram_writes_after_reset=0)
    if episodes < 32 or episodes % 32 or any(report.get(k) != v for k, v in expected.items()):
        raise ValueError("incomplete, mismatched, or unbalanced game evaluation")
    clears = report.get("game_clear_episodes")
    wins = report.get("rom_successes")
    if (type(clears) is not int or not 0 <= clears <= episodes or
            type(wins) is not int or not 0 <= wins <= clears or
            report.get("native_successes") != wins):
        raise ValueError("invalid game clear/win counts")
    progress = report.get("mean_progress_fraction")
    if not isinstance(progress, (int, float)) or not math.isfinite(progress) or not 0 <= progress <= 1:
        raise ValueError("invalid game progress fraction; rebuild sim2rom for schema 3")
    clear_rate = clears / episodes
    return dict(game_score=0.5 * (clear_rate + progress),
                game_clear_rate=clear_rate, game_progress_fraction=progress)


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
    for key, expected in (("base.load_model_path", "None"), ("env.mode", "mixed")):
        if values[key] != expected:
            raise ValueError(f"{key} must be {expected}")
    for key in ("fpg.curriculum_resume", "train.anneal_lr", "train.anneal_ent_coef"):
        if float(values[key]) != 0:
            raise ValueError(f"{key} must be disabled")
        if "sweep." + key in config and any(float(config["sweep." + key][k]) != 0 for k in ("min", "max")):
            raise ValueError(f"cannot sweep {key} in the fresh, non-annealed profile")
    for key in ("vec.total_agents", "vec.num_buffers", "vec.num_threads", "fpg.fraction"):
        if "sweep." + key in config and any(
                float(config["sweep." + key][k]) != float(values[key]) for k in ("min", "max")):
            raise ValueError(f"keep {key} fixed")
    if float(values["fpg.fraction"]) != 1 - GAME_WEIGHT:
        raise ValueError("mixed sweep keeps the 75% game / 25% FPG allocation")
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
    agents = config.getint("vec", "total_agents")
    buffers = config.getint("vec", "num_buffers")
    if buffers < 1 or agents % buffers:
        raise ValueError("buffers must divide the actor count")
    replay = float(config["sweep.train.replay_ratio"]["min"])
    for horizon, minibatch in itertools.product(
            integer_choices(config, "train.horizon"), integer_choices(config, "train.minibatch_size")):
        if (horizon < 8 or minibatch < horizon or horizon % 8 or minibatch % horizon or agents % (minibatch // horizon) or
                int(replay * agents * horizon / minibatch) < 1):
            raise ValueError("illegal rollout/minibatch shape or zero optimizer updates")
        completed_steps(steps, agents, horizon)
    game_episodes = config.getint("sweep", "eval_game_episodes")
    if game_episodes < 32 or game_episodes % 32:
        raise ValueError("game evaluation must cover all 32 stages equally")
    for key in ("max_runs", "eval_episodes", "eval_game_max_frames", "eval_max_frames"):
        if config.getint("sweep", key) < 1:
            raise ValueError(f"sweep.{key} must be positive")
    return ranges


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
        if (values["env.mode"] != "mixed" or values["base.load_model_path"] != "None" or
                float(values["fpg.curriculum_resume"]) != 0 or
                float(values["train.anneal_lr"]) != 0 or float(values["train.anneal_ent_coef"]) != 0 or
                float(values["fpg.fraction"]) != 1 - GAME_WEIGHT):
            raise ValueError("mixed sweep requires fresh weights/curriculum and both annealings off")
        requested_steps = int(float(values["train.total_timesteps"]))
        steps = completed_steps(requested_steps, int(float(values["vec.total_agents"])),
                                int(float(values["train.horizon"])))
        checkpoint = ROOT / values["base.checkpoint_dir"] / ENV / run_id / f"{steps:016d}.bin"
        if checkpoint.parent.exists():
            raise ValueError(f"checkpoint directory already exists: {checkpoint.parent}")
        train_values = values | {"base.result_fd": "0", "base.eval_episodes": "0",
                                 "base.checkpoint_interval": "0"}
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
        game_episodes = int(float(values["sweep.eval_game_episodes"]))
        game_frame_cap = int(float(values["sweep.eval_game_max_frames"]))
        evaluation = configparser.ConfigParser(interpolation=None)
        evaluation["policy"] = {k: values[f"policy.{k}"] for k in ("hidden_size", "num_layers")}
        evaluation["env"] = {k: values[f"env.{k}"] for k in
                             ("engine_cpu_archive", "engine_module")}
        evaluation["fpg"] = {"time_table": values["fpg.time_table"], "max_frames": str(frame_cap)}
        evaluation["game"] = dict(max_frames=str(game_frame_cap), clear_reward="1",
                                  time_bonus="0.1", time_target_frames="1800", death_penalty="0",
                                  checkpoint_distance="128", checkpoint_reward="0.025")
        eval_config = directory / "evaluation.ini"
        with eval_config.open("x") as stream:
            evaluation.write(stream)
        game_output = directory / "game"
        command = [str(ROOT / values["sweep.evaluator_path"]), str(checkpoint),
                   str(eval_config), str(game_output), str(game_episodes), str(seed), "0",
                   "all-stages", "0", "game"]
        run_logged(command, directory / "eval_game.log")
        game_report = json.loads((game_output / "summary.json").read_text())
        breakdown = game_panel_score(game_report, game_episodes, seed, game_frame_cap)
        reports = []
        for depth in depths:
            output = directory / f"depth_{depth}"
            command = [str(ROOT / values["sweep.evaluator_path"]), str(checkpoint),
                       str(eval_config), str(output), str(episodes), str(seed), "0",
                       str(ROOT / values["sweep.eval_bank"]), str(depth), "fpg"]
            run_logged(command, directory / f"eval_{depth}.log")
            reports.append(json.loads((output / "summary.json").read_text()))
        fpg_rate = panel_score(reports, depths, episodes, seed, frame_cap)
        score = GAME_WEIGHT * breakdown["game_score"] + (1 - GAME_WEIGHT) * fpg_rate
        breakdown["fpg_rate"] = fpg_rate
        seconds = time.monotonic() - started
        receipt.update(status="complete", score=score, objective="fixed_mixed_rom_score",
                       score_breakdown=breakdown, seconds=seconds, steps=steps,
                       requested_steps=requested_steps, checkpoint=str(checkpoint),
                       checkpoint_sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
                       game_evaluation=game_report, evaluations=reports)
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
    print(f"{len(results)} completed trials; fixed mixed ROM score (higher is better)")
    for result in results[:10]:
        parts = result.get("score_breakdown", {})
        print(f"{result['score']:.4f}  clears={parts.get('game_clear_rate', 0):.3f} "
              f"progress={parts.get('game_progress_fraction', 0):.3f} "
              f"FPG={parts.get('fpg_rate', 0):.3f}  "
              f"{result['seconds'] / 60:.1f} min  {result['checkpoint']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, default=HERE / "profiles/sweep.ini")
    parser.add_argument("--runs", type=int)
    parser.add_argument("--steps", type=int)
    parser.add_argument("--eval-episodes", type=int, help="FPG episodes at each exact depth")
    parser.add_argument("--game-eval-episodes", type=int, help="multiple of 32, one or more per stage")
    parser.add_argument("--game-eval-frames", type=int, help="fixed game episode frame cap")
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
    if args.game_eval_episodes is not None:
        config["sweep"]["eval_game_episodes"] = str(args.game_eval_episodes)
    if args.game_eval_frames is not None:
        config["sweep"]["eval_game_max_frames"] = str(args.game_eval_frames)
    steps = config.getint("train", "total_timesteps")
    config["sweep"]["worker_path"] = str(Path(__file__).resolve())
    # Never inherit the normal training continuation, including after local edits.
    config["base"]["load_model_path"] = "None"
    config["fpg"]["curriculum_resume"] = "0"
    config["env"]["mode"] = "mixed"
    config["train"]["anneal_lr"] = "0"
    config["train"]["anneal_ent_coef"] = "0"
    try:
        ranges = validate_search(config)
    except ValueError as error:
        parser.error(str(error))
    agents = config.getint("vec", "total_agents")
    budgets = [completed_steps(steps, agents, h) for h in integer_choices(config, "train.horizon")]
    if args.dry_run:
        print(json.dumps(dict(runs=config.getint("sweep", "max_runs"), steps_per_trial=steps,
                              completed_steps_range=[min(budgets), max(budgets)],
                              total_steps=steps * config.getint("sweep", "max_runs"),
                              fresh_weights=True, mode=config["env"]["mode"],
                              actors=agents, buffers=config.getint("vec", "num_buffers"),
                              threads=config.getint("vec", "num_threads"), annealing=False,
                              evaluation=dict(config["sweep"]),
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
    print(f"Starting {config['sweep']['max_runs']} trials x {steps:,} requested frames "
          f"({min(budgets):,}–{max(budgets):,} after rollout rounding); results: {output}", flush=True)
    print(f"Progress: tail -f {output / 'sweep.log'}", flush=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run_logged(command, output / "sweep.log", process_group=True)
    show_results(output)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "train":
        run_trial(worker_arguments(sys.argv[2:]))
    else:
        main()
