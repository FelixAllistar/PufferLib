# /// script
# requires-python = ">=3.10"
# dependencies = ["ribs==0.12.0"]
# ///
"""Reward-only CMA-MAE around ordinary native train/match processes.

Prepare snapshots inputs but never starts a worker. Run explicitly starts/resumes
the prepared experiment. Only load state.pkl created by this program yourself.
"""
import argparse
import asyncio
import configparser
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import pickle
import re
import shutil
import signal
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
REWARDS = ("reward_growth_crop", "reward_growth_animal", "reward_alive_daily",
    "reward_quality_scale", "reward_quality_idle_cost")
MEASURES = ("animal_value_share", "animal_delay")
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
METRIC = re.compile(r"\b([a-z][a-z_0-9]*)\s+(" + NUMBER + r")(?=\s|$)")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    tmp.replace(path)


def read_config(path):
    config = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=("#",))
    assert config.read(path), path
    return {f"{section}.{key}": value.strip("'\"")
        for section in config.sections() for key, value in config[section].items()
        if section != "bc" and not section.startswith("sweep.")}


def write_config(path, values):
    config = configparser.ConfigParser(interpolation=None)
    for name, value in values.items():
        section, key = name.split(".", 1)
        if section not in config:
            config[section] = {}
        config[section][key] = str(value)
    with path.open("w") as stream:
        config.write(stream)


def prepare(args):
    config = read_config(ROOT / "config/default.ini")
    config.update(read_config(args.config))
    assert config["base.env_name"] == "kaggriculture"
    assert int(config["policy.hidden_size"]) == 256 and int(config["policy.num_layers"]) == 2
    assert int(config["env.market_slots"]) == 10 and int(config["env.max_hands"]) == 16
    assert int(config["env.land_buy_min_days"]) == 0
    assert args.bc.is_file() and args.bc.stat().st_size == 4328800, "H256/L2 BC required"
    bc_metadata = json.loads(args.bc.with_suffix(".json").read_text())
    assert bc_metadata["sha256"] == digest(args.bc), "BC metadata hash mismatch"
    assert bc_metadata["terminal_critic"] is False, "actor-only BC initializer required"
    assert bc_metadata["contract"]["hidden"] == 256 and bc_metadata["contract"]["layers"] == 2
    assert args.binary.is_file(), args.binary
    opponents = [(ROOT / line.strip()).resolve() for line in args.opponents.read_text().splitlines()
        if line.strip()]
    assert opponents and all(p.is_file() and p.stat().st_size == 4328800 for p in opponents)
    assert int(config["vec.num_policies"]) == len(opponents) + 1
    assert int(config["selfplay.enabled"]) == 1 and int(config["env.num_agents"]) == 2
    assert float(config["selfplay.opp_timeout_steps"]) == 0, "freeze the opponent panel"
    assert int(config["vec.hist_policy_hidden_size"]) == 256
    assert int(config["vec.hist_policy_num_layers"]) == 2
    agents, horizon, mb = (int(config[k]) for k in
        ("vec.total_agents", "train.horizon", "train.minibatch_size"))
    assert args.steps >= agents * horizon and mb % horizon == 0
    assert agents % (mb // horizon) == 0
    assert agents % int(config["vec.num_buffers"]) == 0
    profile = configparser.ConfigParser()
    assert profile.read(args.settings)
    assert set(profile["rewards"]) == set(REWARDS), "only the five auxiliary rewards vary"
    ranges = [list(map(float, profile["rewards"][key].split(","))) for key in REWARDS]
    assert all(len(pair) == 2 and 0 <= pair[0] < pair[1] and
        all(math.isfinite(v) for v in pair) for pair in ranges)
    assert ranges[-1][1] <= 1
    q = profile["qd"]
    settings = dict(max_trials=int(q["max_trials"]), batch_size=int(q["batch_size"]),
        archive_bins=list(map(int, q["archive_bins"].split(","))),
        archive_alpha=float(q["archive_alpha"]), sigma=float(q["sigma"]), seed=int(q["seed"]),
        eval_games=int(q["eval_games"]), eval_seed=int(q["eval_seed"]),
        display_seconds=float(q["display_seconds"]))
    assert settings["batch_size"] >= 4
    assert settings["max_trials"] > 0 and settings["max_trials"] % settings["batch_size"] == 0
    assert len(settings["archive_bins"]) == 2 and min(settings["archive_bins"]) > 0
    assert 0 < settings["archive_alpha"] <= 1 and settings["sigma"] > 0
    assert settings["eval_games"] >= 2 and settings["eval_games"] % 2 == 0
    assert settings["display_seconds"] >= 1
    if float(config["env.reset_state_prob"]) > 0:
        assert (ROOT / config["env.reset_state_bank"]).is_file(), "reset bank is required"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.bc, output / "bc.bin")
    copies = []
    for i, source in enumerate(opponents):
        target = output / f"opponent_{i}.bin"
        shutil.copyfile(source, target)
        copies.append(str(target))
    (output / "opponents.txt").write_text("".join(p + "\n" for p in copies))
    config.update({"base.load_model_path": str(output / "bc.bin"),
        "base.load_enemy_model_path": "None", "base.checkpoint_dir": str(output / "checkpoints"),
        "base.log_dir": str(output / "native_logs"), "base.checkpoint_interval": "0",
        "base.eval_episodes": "0", "base.result_fd": "0", "base.gpu_offset": "0",
        "base.run_id": "None", "train.gpus": "1", "train.total_timesteps": str(args.steps),
        "env.reward_money": "1", "env.reward_growth_land": "0", "env.potential_beta": "0",
        "train.reward_clip": "0", "selfplay.initial_opponents": str(output / "opponents.txt"),
        "selfplay.eval_games": "0", "selfplay.eval_bot_games": "0",
        "sweep.metric": "root_money", "sweep.downsample": "1"})
    config.update({f"env.{key}": "0" for key in REWARDS})
    write_config(output / "fixed.ini", config)
    files = [args.binary.resolve(), output / "bc.bin", output / "fixed.ini",
        output / "opponents.txt", *map(Path, copies)]
    if float(config["env.reset_state_prob"]) > 0:
        files.append((ROOT / config["env.reset_state_bank"]).resolve())
    manifest = dict(version=1, binary=str(args.binary.resolve()), config=config, settings=settings,
        rewards=list(REWARDS), ranges=ranges, measures=list(MEASURES), opponents=copies,
        bc_metadata=bc_metadata,
        bc_source=str(args.bc.resolve()), config_source=str(args.config.resolve()),
        files={str(path): digest(path) for path in files})
    write_json(output / "manifest.json", manifest)
    print(f"Prepared {output}; no workers started. BC sha256={digest(output / 'bc.bin')}")
    print(f"Fixed PPO: agents={agents} H={horizon} MB={mb} LR={config['train.learning_rate']} "
        f"steps={args.steps} resets={config['env.reset_state_prob']}")


class Dashboard:
    """Decode native dashboards from a pipe (no terminal truncation/escape cursor moves)."""
    def __init__(self):
        self.frame = {}
        self.latest = {}
        self.environment = {}
        self.summary = None

    def feed(self, line):
        line = ANSI.sub("", line)
        if line.startswith("KAG_QD_METRICS "):
            self.environment = json.loads(line[len("KAG_QD_METRICS "):])
            self.latest.update(self.environment)
            return self.latest.copy()
        if "╭" in line:
            self.frame = {}
        for key, value in METRIC.findall(line):
            self.frame[key] = float(value)
        for key, name in (("Steps", "steps"), ("SPS", "sps")):
            match = re.search(r"\b" + key + r"\s+(" + NUMBER + r")([KMB]?)\b", line)
            if match:
                self.frame[name] = float(match[1]) * {"": 1, "K": 1e3, "M": 1e6,
                    "B": 1e9}[match[2]]
        if line.startswith("CUDA_EVAL "):
            self.summary = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if "╰" not in line:
            return None
        self.latest.update(self.frame)
        self.latest.update(self.environment)
        return self.latest.copy()


def status_line(gpu, status):
    metrics = status.get("metrics", {})
    def number(key, fmt):
        return format(metrics[key], fmt) if key in metrics else "—"
    reset = "—"
    if "reset_fraction" in metrics:
        reset = number("reset_money", ".0f") if metrics["reset_fraction"] > 0 else "off"
    steps = f"{metrics['steps']/1e6:.1f}M" if "steps" in metrics else "—"
    return (f"GPU{gpu} {status.get('trial', '—')} {status.get('phase', 'idle')} "
        f"{steps} SPS={number('sps', '.0f')} root$={number('root_money', '.0f')} "
        f"reset$={reset} plants={number('plants', '.1f')} "
        f"animals={number('animal_places', '.1f')} plots={number('ending_plots', '.2f')}")


async def command(binary, mode, values, gpu, path, status):
    argv = [binary, mode, "--headless"] + [f"--{k}={v}" for k, v in values.items()]
    write_json(path.with_suffix(".command.json"), argv)
    env = dict(os.environ, CUDA_VISIBLE_DEVICES=str(gpu), OMP_NUM_THREADS="1",
        OPENBLAS_NUM_THREADS="1", KAG_QD_METRICS="1")
    process = await asyncio.create_subprocess_exec(*argv, cwd=ROOT, env=env,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT,
        start_new_session=True)
    dashboard = Dashboard()
    try:
        with path.open("w") as log, path.with_suffix(".jsonl").open("w") as progress:
            async for raw in process.stdout:
                line = raw.decode(errors="replace")
                log.write(line)
                log.flush()
                frame = dashboard.feed(line)
                if frame:
                    status["metrics"] = frame
                    progress.write(json.dumps(dict(time=time.time(), **frame)) + "\n")
                    progress.flush()
        code = await process.wait()
        assert code == 0, f"worker exited {code}; see {path}"
    finally:
        if process.returncode is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                await asyncio.wait_for(process.wait(), 5)
            except asyncio.TimeoutError:
                os.killpg(process.pid, signal.SIGKILL)
                await process.wait()
    return dashboard


def candidate_config(manifest, trial, solution):
    config = manifest["config"].copy()
    config["base.run_id"] = f"qd_{trial:04d}"
    # Fixed seed deliberately pairs stochastic training conditions across reward vectors.
    for key, x, (low, high) in zip(REWARDS, solution, manifest["ranges"]):
        assert 0 <= x <= 1
        config[f"env.{key}"] = format(low + x * (high - low), ".9g")
    return config


def evaluation_config(manifest, checkpoint, opponent, seed):
    config = manifest["config"].copy()
    games = manifest["settings"]["eval_games"]
    config.update({"base.load_model_path": str(checkpoint),
        "base.load_enemy_model_path": opponent, "base.eval_episodes": str(games),
        "base.eval_agents": str(2 * min(games, 128)), "base.seed": str(seed),
        "base.async": "0", "base.cudagraphs": "1", "selfplay.enabled": "0",
        "vec.total_agents": str(2 * min(games, 128)), "vec.num_buffers": "1",
        "vec.num_policies": "1", "env.num_agents": "2", "env.reset_state_prob": "0",
        "train.horizon": "64", "train.minibatch_size": "64", "sweep.metric": "score"})
    config.update({f"env.{key}": "0" for key in REWARDS})
    return config


def measure_results(results):
    required = ("root_money", "crop_ref_value", "animal_ref_value", "animal_delay",
        "animal_seen", "plot2_delay", "plot3_delay", "plants", "animal_places", "ending_plots")
    assert all(all(k in r["metrics"] for k in required) for r in results), \
        "Missing QD telemetry: rebuild Kaggriculture before starting this search"
    total = sum(r["games"] for r in results)
    metrics = {k: sum(r["games"] * r["metrics"][k] for r in results) / total for k in required}
    metrics["win_rate"] = sum(r["games"] * r["win_rate"] for r in results) / total
    value = metrics["crop_ref_value"] + metrics["animal_ref_value"]
    measures = [metrics["animal_ref_value"] / value if value else 0, metrics["animal_delay"]]
    assert all(math.isfinite(v) for v in metrics.values()) and metrics["root_money"] >= 0
    assert all(0 <= v <= 1 for v in measures), measures
    return dict(objective=metrics["root_money"], measures=measures, metrics=metrics, games=total)


async def evaluate(manifest, checkpoint, directory, gpu, status):
    results = []
    for i, opponent in enumerate(manifest["opponents"]):
        status.update(phase=f"eval{i+1}/{len(manifest['opponents'])}", metrics={})
        values = evaluation_config(manifest, checkpoint, opponent,
            manifest["settings"]["eval_seed"] + i)
        dashboard = await command(manifest["binary"], "match", values, gpu,
            directory / f"eval_{i}.log", status)
        assert dashboard.summary, "match did not report completion"
        games = int(dashboard.summary["games"])
        assert games == manifest["settings"]["eval_games"], "unexpected evaluation game count"
        results.append(dict(games=games, win_rate=float(dashboard.summary["score"]),
            metrics=dashboard.latest))
    result = measure_results(results)
    result["panel"] = results
    return result


def new_search(manifest):
    import numpy as np
    from ribs.archives import GridArchive
    from ribs.emitters import EvolutionStrategyEmitter
    from ribs.schedulers import Scheduler
    settings = manifest["settings"]
    args = dict(solution_dim=len(REWARDS), dims=settings["archive_bins"],
        ranges=[(0, 1), (0, 1)], extra_fields={"trial": ((), np.int64)}, seed=settings["seed"])
    archive = GridArchive(**args, learning_rate=settings["archive_alpha"], threshold_min=0)
    result_archive = GridArchive(**args)
    emitter = EvolutionStrategyEmitter(archive, x0=np.full(len(REWARDS), 0.5),
        sigma0=settings["sigma"], ranker="imp", selection_rule="mu", restart_rule="basic",
        bounds=[(0, 1)] * len(REWARDS), batch_size=settings["batch_size"], seed=settings["seed"])
    return dict(scheduler=Scheduler(archive, [emitter]), result_archive=result_archive,
        next_trial=0, pending=None)


def save_search(output, state):
    path = output / "state.tmp"
    with path.open("wb") as stream:
        pickle.dump(state, stream)
    path.replace(output / "state.pkl")


def finish_batch(state, results):
    import numpy as np
    ids = [r["trial"] for r in results]
    # Failed processes are NOT elites, nor viable zero-score behavioral samples.
    objectives = [r["objective"] if r["ok"] else -1.0 for r in results]
    measures = [r["measures"] if r["ok"] else [0, 1] for r in results]
    state["scheduler"].tell(objectives, measures, trial=ids)
    valid = [i for i, r in enumerate(results) if r["ok"]]
    if valid:
        solutions = state["pending"]["solutions"]
        state["result_archive"].add(np.asarray(solutions)[valid], np.asarray(objectives)[valid],
            np.asarray(measures)[valid], trial=np.asarray(ids)[valid])
    state["next_trial"] += len(results)
    state["pending"] = None


async def run_search(output, manifest, gpu_ids):
    import numpy as np
    state_path = output / "state.pkl"
    if state_path.exists():
        with state_path.open("rb") as stream:
            state = pickle.load(stream)
    else:
        state = new_search(manifest)
    slots = asyncio.Queue()
    statuses = {gpu: {} for gpu in gpu_ids}
    for gpu in gpu_ids:
        slots.put_nowait(gpu)
    asyncio.get_running_loop().add_signal_handler(signal.SIGTERM, asyncio.current_task().cancel)

    async def display():
        tty = sys.stdout.isatty()
        drawn = False
        while True:
            if tty and drawn:
                print(f"\033[{len(gpu_ids)}A", end="")
            for gpu in gpu_ids:
                line = status_line(gpu, statuses[gpu])
                if tty:
                    width = shutil.get_terminal_size().columns
                    line = "\033[2K" + line[:max(1, width - 1)]
                print(line, flush=True)
            drawn = True
            await asyncio.sleep(manifest["settings"]["display_seconds"])

    async def trial_job(trial, solution):
        directory = output / f"trial_{trial:04d}"
        directory.mkdir(exist_ok=True)
        result_path = directory / "result.json"
        if result_path.exists():
            return json.loads(result_path.read_text())
        gpu = await slots.get()
        status = statuses[gpu]
        status.update(trial=f"#{trial}", phase="train", metrics={})
        started = time.time()
        try:
            config = candidate_config(manifest, trial, solution)
            write_config(directory / "config.ini", config)
            trained = directory / "trained.json"
            if not trained.exists():
                await command(manifest["binary"], "train", config, gpu, directory / "train.log", status)
                ckpt_dir = Path(config["base.checkpoint_dir"]) / "kaggriculture" / config["base.run_id"]
                initial = ckpt_dir / "0000000000000000.bin"
                assert digest(initial) == digest(manifest["config"]["base.load_model_path"]), \
                    "native initial checkpoint does not match BC"
                paths = sorted(ckpt_dir.glob("*.bin"), key=lambda p: int(p.stem))
                batch = int(config["vec.total_agents"]) * int(config["train.horizon"])
                expected = int(config["train.total_timesteps"]) // batch * batch
                assert paths and int(paths[-1].stem) == expected, "final checkpoint missing"
                write_json(trained, dict(checkpoint=str(paths[-1]), sha256=digest(paths[-1])))
            saved = json.loads(trained.read_text())
            assert digest(saved["checkpoint"]) == saved["sha256"], "trained checkpoint changed"
            result = await evaluate(manifest, saved["checkpoint"], directory, gpu, status)
            result.update(ok=True, checkpoint=saved["checkpoint"], checkpoint_sha256=saved["sha256"])
        except (AssertionError, OSError, ValueError) as error:
            result = dict(ok=False, error=str(error))
        finally:
            status.update(phase="idle")
            slots.put_nowait(gpu)
        result.update(trial=trial, seconds=time.time() - started, solution=list(solution))
        write_json(result_path, result)
        return result

    ui = asyncio.create_task(display())
    try:
        # Fail early on an old binary lacking behavior metrics; evaluate BC once, no training.
        if not (output / "baseline.json").exists():
            directory = output / "baseline"
            directory.mkdir(exist_ok=True)
            statuses[gpu_ids[0]]["trial"] = "BC"
            baseline = await evaluate(manifest, output / "bc.bin", directory, gpu_ids[0],
                statuses[gpu_ids[0]])
            write_json(output / "baseline.json", baseline)
            statuses[gpu_ids[0]]["phase"] = "idle"
        while state["next_trial"] < manifest["settings"]["max_trials"]:
            if state["pending"] is None:
                state["pending"] = dict(solutions=state["scheduler"].ask().tolist())
                save_search(output, state)
            solutions = state["pending"]["solutions"]
            results = await asyncio.gather(*(trial_job(state["next_trial"] + i, solution)
                for i, solution in enumerate(solutions)))
            finish_batch(state, results)
            save_search(output, state)
            data = state["result_archive"].data()
            elites = [dict(trial=int(trial), objective=float(objective), measures=measures.tolist(),
                solution=solution.tolist(), result=str(output / f"trial_{trial:04d}" / "result.json"))
                for trial, objective, measures, solution in zip(data["trial"], data["objective"],
                    data["measures"], data["solution"])]
            write_json(output / "elites.json", elites)
            summary = dict(completed=state["next_trial"], cells=len(elites),
                total_cells=int(np.prod(manifest["settings"]["archive_bins"])),
                best_money=max((e["objective"] for e in elites), default=0),
                last_batch_failed=sum(not r["ok"] for r in results))
            write_json(output / "status.json", summary)
            assert any(r["ok"] for r in results), "entire batch failed; inspect trial logs before resuming"
    finally:
        ui.cancel()
        await asyncio.gather(ui, return_exceptions=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="mode", required=True)
    p = commands.add_parser("prepare", help="snapshot a reward-only search; never launches training")
    p.add_argument("--config", type=Path, default=ROOT / "config/kaggriculture.ini")
    p.add_argument("--settings", type=Path, default=Path(__file__).with_name("profiles") / "qd.ini")
    p.add_argument("--binary", type=Path, default=ROOT / "puffer")
    p.add_argument("--bc", type=Path, default=ROOT / "saved/kaggriculture/initial_bc.bin")
    p.add_argument("--opponents", type=Path, required=True)
    p.add_argument("--steps", type=int, required=True, help="fixed per-candidate budget; choose explicitly")
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("run", help="start/resume an explicitly prepared search")
    p.add_argument("output", type=Path)
    p.add_argument("--gpus", default="0,1", help="physical CUDA device ids (one worker each)")
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args)
        return
    output = args.output.resolve()
    manifest = json.loads((output / "manifest.json").read_text())
    assert manifest["version"] == 1
    for path, expected in manifest["files"].items():
        assert digest(path) == expected, f"input changed: {path}; prepare a new experiment"
    gpu_ids = args.gpus.split(",")
    assert gpu_ids and len(set(gpu_ids)) == len(gpu_ids) and all(x.isdigit() for x in gpu_ids)
    with (output / "run.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        asyncio.run(run_search(output, manifest, gpu_ids))
    print("QD complete:", (output / "status.json").read_text().strip())


if __name__ == "__main__":
    main()
