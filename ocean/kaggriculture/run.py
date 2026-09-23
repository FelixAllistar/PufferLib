"""Apply an experiment profile as ordinary native CLI overrides.

uv run --no-project python ocean/kaggriculture/run.py train --profile terminal
uv run --no-project python ocean/kaggriculture/run.py train --profile shaped
The default config remains usable directly with ./puffer train.
"""

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import struct
import time


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = {"observation": 3, "policy": 5, "macro": 2, "executor": 2,
    "hidden": 256, "layers": 2, "parameters": 1082200}


def save_league(path, league):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(league, indent=2) + "\n")
    temporary.replace(path)


def solve_league(league):
    import numpy as np
    from scipy.optimize import linprog

    names = list(league["models"])
    payoff = np.zeros((len(names), len(names)))
    for i, first in enumerate(names):
        for j in range(i + 1, len(names)):
            second = names[j]
            result = league["matches"][f"{first}/{second}"]
            payoff[i, j] = 2 * result["score"] - 1
            payoff[j, i] = -payoff[i, j]
    solution = linprog(np.r_[np.zeros(len(names)), -1],
        A_ub=np.c_[-payoff.T, np.ones(len(names))], b_ub=np.zeros(len(names)),
        A_eq=[np.r_[np.ones(len(names)), 0]], b_eq=[1],
        bounds=[(0, 1)] * len(names) + [(-1, 1)], method="highs")
    assert solution.success, solution.message
    weights = np.maximum(solution.x[:-1], 0)
    weights /= weights.sum()
    league["strategy"] = dict(zip(names, weights.tolist()))
    scores = payoff @ weights
    previous = league.get("champion")
    best = names[int(np.argmax(scores))]
    if previous in names and scores[names.index(previous)] >= max(scores) - 1e-8:
        best = previous
    league["champion"] = best
    return league["strategy"]


def evaluate(binary, directory, first, second, games, seed, bot=None, seat=0):
    # Every match includes both seats. Bot evaluation uses separate seat runs.
    agents = min(games, 128) * (2 if second else 1)
    options = {
        "base.load_model_path": first, "base.load_enemy_model_path": second or "None",
        "base.eval_episodes": games, "base.eval_agents": agents,
        "base.seed": seed, "base.async": 0, "base.cudagraphs": 1,
        "policy.hidden_size": 256, "policy.num_layers": 2,
        "vec.total_agents": agents, "vec.num_buffers": 1, "vec.num_policies": 1,
        "selfplay.enabled": 0, "env.num_agents": 2 if second else 1,
        "env.learner_seat": seat, "env.bot_policy": bot or 0,
        "env.reset_state_prob": 0, "train.horizon": 64, "train.minibatch_size": 64,
        "sweep.metric": "score",
    }
    command = [str(binary.resolve()), "match" if second else "eval", "--headless"]
    command += [f"--{key}={value}" for key, value in options.items()]
    directory.parent.mkdir(parents=True, exist_ok=True)
    print(shlex.join(command), flush=True)
    with directory.open("x") as log:
        process = subprocess.Popen(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        summary = None
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
            found = re.search(r"CUDA_EVAL env=\S+ score=(\S+) perf=(\S+) games=(\d+)", line)
            if found:
                summary = {"score": float(found[1]), "perf": float(found[2]),
                    "games": int(found[3]), "seed": seed, "log": str(directory)}
        assert process.wait() == 0 and summary is not None, directory
    return summary


def league_command(args):
    path = args.league.resolve()
    league = json.loads(path.read_text()) if path.exists() else {
        "contract": CONTRACT, "models": {}, "matches": {}, "strategy": {}}
    assert league["contract"] == CONTRACT
    if args.mode == "league-add":
        assert args.checkpoint and args.name, "--checkpoint and --name are required"
        assert re.fullmatch(r"[A-Za-z0-9_-]+", args.name), "simple unique name required"
        assert args.name not in league["models"], "name is already registered"
        source = args.checkpoint.resolve()
        data = source.read_bytes()
        assert len(data) == 4 * CONTRACT["parameters"], "wrong H256/L2 checkpoint size"
        digest = hashlib.sha256(data).hexdigest()
        assert all(model["sha256"] != digest for model in league["models"].values()), \
            "checkpoint already registered under another name"
        destination = path.parent / "pool" / f"{args.name}_{digest[:12]}.bin"
        destination.parent.mkdir(parents=True, exist_ok=True)
        assert not destination.exists(), destination
        shutil.copyfile(source, destination)
        league["models"][args.name] = {
            "path": str(destination.relative_to(path.parent)), "sha256": digest,
            "source": str(source)}
        league["strategy"] = {}  # new matrix entries must be evaluated first
    elif args.mode == "league-eval":
        assert args.games >= 2 and args.games % 2 == 0, "even game count balances seats"
        names = list(league["models"])
        assert names, "add checkpoints first"
        paths = {name: (path.parent / model["path"]).resolve()
            for name, model in league["models"].items()}
        for name, model in league["models"].items():
            assert hashlib.sha256(paths[name].read_bytes()).hexdigest() == model["sha256"]
        directory = path.parent / "evaluations" / str(time.time_ns())
        for i, first in enumerate(names):
            for second in names[i + 1:]:
                key = f"{first}/{second}"
                if key in league["matches"]:
                    continue
                result = evaluate(args.binary, directory / f"{first}_vs_{second}.log",
                    paths[first], paths[second],
                    args.games, args.seed)
                assert 0 <= result["score"] <= 1
                league["matches"][key] = result
                save_league(path, league)
        solve_league(league)
        league["strategy_source"] = "reset-free balanced-seat matches on converted runtime"
        champion = league["champion"]
        if args.bots:
            for bot in (0, 1):
                for seat in (0, 1):
                    log = directory / f"{champion}_bot{bot}_seat{seat}.log"
                    result = evaluate(args.binary, log,
                        paths[champion], None, args.games, args.seed, bot, seat)
                    league.setdefault("bot_results", {}).setdefault(champion, {})[
                        f"{bot}/{seat}"] = result
                    save_league(path, league)
        print(f"Empirical league champion: {champion}; strategy: {league['strategy']}")
    else:
        import numpy as np

        assert args.banks > 0 and league["strategy"], "evaluate the complete matrix first"
        names = list(league["strategy"])
        # Systematic resampling represents the mixture with less bank-count noise.
        rng = np.random.default_rng(args.seed)
        points = (rng.random() + np.arange(args.banks)) / args.banks
        indices = np.searchsorted(np.cumsum([league["strategy"][name] for name in names]), points)
        chosen = np.asarray(names)[indices]
        rng.shuffle(chosen)
        output = args.opponents.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text("".join(str((path.parent / league["models"][name]["path"]).resolve())
            + "\n" for name in chosen))
        print(f"Frozen banks ({args.seed=}): {list(chosen)} -> {output}")
        return 0
    save_league(path, league)
    return 0


def validate_dataset(path, config, mode):
    with path.open("rb") as stream:
        header = struct.unpack("<16I2Qd", stream.read(88))
    metadata = json.loads(path.with_suffix(".json").read_text())
    assert metadata["format"] == "kaggriculture_entity_bc_v3"
    assert metadata["source_hash"] == f"{header[16]:016x}"
    assert metadata["semantics_hash"] == f"{header[17]:016x}"
    assert metadata["train_games"] + metadata["validation_games"] == header[6]
    source = configparser.ConfigParser(interpolation=None)
    source.read_string(metadata["profile"])
    for old, new in [("policy_market_slots", "market_slots"),
        ("policy_max_hands", "max_hands"), ("land_buy_min_days", "land_buy_min_days")]:
        assert source.getint("env", old) == config.getint("env", new), f"controller mismatch: {new}"
    if mode != "bc":
        assert abs(header[-1] - config.getfloat("train", "gamma")) < 1e-8, "gamma mismatch"
        assert source.getfloat("train", "reward_clip") == 0
        assert config.getfloat("train", "reward_clip") == 0
        assert source.getfloat("env", "reward_money_timing") == 0
        assert source.getfloat("env", "reward_pbrs_scale") == 0
        for key in ["growth_land", "growth_crop", "growth_animal", "alive_daily", "quality_scale",
            "quality_idle_cost", "target_plots", "target_crops", "target_animals", "money"]:
            original = "reward_money_scale" if key == "money" else f"reward_{key}"
            assert abs(source.getfloat("env", original) - config.getfloat("env", f"reward_{key}")) \
                < 1e-7, f"expert-return reward mismatch: {key}"
    return {"source_hash": metadata["source_hash"], "semantics_hash": metadata["semantics_hash"],
        "train_games": metadata["train_games"], "validation_games": metadata["validation_games"],
        "gamma": header[-1], "teacher": metadata["teacher"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["train", "eval", "match", "sweep", "league-add",
        "league-eval", "league-sample", "build-bc", "bc", "critic", "bc-critic"])
    parser.add_argument("--profile", choices=["terminal", "shaped"], default="terminal")
    parser.add_argument("--binary", type=Path, default=ROOT / "puffer")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--league", type=Path, default=ROOT / "saved/kaggriculture/league.json")
    parser.add_argument("--checkpoint", type=Path)
    parser.add_argument("--name")
    parser.add_argument("--banks", type=int, default=4)
    parser.add_argument("--games", type=int, default=64)
    parser.add_argument("--seed", type=int, default=707)
    parser.add_argument("--bots", action="store_true")
    parser.add_argument("--opponents", type=Path,
        default=ROOT / "saved/kaggriculture/initial_opponents.txt")
    parser.add_argument("--bc-binary", type=Path, default=ROOT / "build/kag_bc")
    parser.add_argument("--arch", default=os.environ.get("NVCC_ARCH", "native"))
    parser.add_argument("--precision", choices=["fp32", "bf16"], default="bf16")
    args, overrides = parser.parse_known_args()
    if args.mode.startswith("league-"):
        assert not overrides and not args.dry_run, "league commands take explicit arguments"
        return league_command(args)
    if args.mode == "build-bc":
        cuda = Path(os.environ.get("CUDA_HOME", "/usr/local/cuda"))
        command = [str(cuda / "bin/nvcc"), "-O2", "--threads", "2", f"-arch={args.arch}",
            "-std=c++17", "-I.", "-Isrc", "-Ivendor", "-Iraylib-5.5_linux_amd64/include",
            f"-I{cuda}/include/cccl", "-DPUFFER_KAGGRICULTURE", "-DENV_NAME=kaggriculture",
            '-DPUFFER_ENV_NAME="kaggriculture"',
            '-DENV_HEADER="ocean/kaggriculture/kaggriculture.cu"',
            "-Xcompiler=-fopenmp", "-Xcompiler=-Wno-narrowing", "--diag-suppress=2361",
            "--diag-suppress=111", "--diag-suppress=128", "ocean/kaggriculture/bc.cu",
            "raylib-5.5_linux_amd64/lib/libraylib.a", f"-L{cuda}/lib64", "-lcudart", "-lnccl",
            "-lnvidia-ml", "-lcublas", "-lcusolver", "-lcurand", "-lm", "-lpthread", "-lomp5",
            "-lGL", "-o", str(args.bc_binary.resolve())]
        if args.precision == "fp32":
            command.append("-DPRECISION_FLOAT")
        print(shlex.join(command), flush=True)
        if args.dry_run:
            return 0
        assert not overrides
        args.bc_binary.parent.mkdir(parents=True, exist_ok=True)
        return subprocess.run(command, cwd=ROOT).returncode
    config = configparser.ConfigParser(interpolation=None)
    with Path(__file__).with_name("profiles").joinpath(f"{args.profile}.ini").open() as stream:
        config.read_file(stream)
    command = [str(args.binary.resolve()), args.mode]
    command += [f"--{section}.{key}={value}"
        for section in config.sections() for key, value in config[section].items()]
    if args.mode in ("eval", "match"):
        command.append("--headless")
        command.append("--env.reset_state_prob=0")
        command.append("--base.eval_episodes=64")
        command.append(f"--env.num_agents={2 if args.mode == 'match' else 1}")
        command.append("--selfplay.enabled=0")
        command.append("--vec.num_policies=1")
    command += overrides
    if args.mode in ("bc", "critic", "bc-critic"):
        command = [str(args.bc_binary.resolve())] + command[2:]
        mode = {"bc": "actor", "critic": "critic", "bc-critic": "joint"}[args.mode]
        command.append(f"--bc.mode={mode}")
        settings = configparser.ConfigParser(interpolation=None)
        settings.read([ROOT / "config/default.ini", ROOT / "config/kaggriculture.ini"])
        for option in command[1:]:
            key, value = option.removeprefix("--").split("=", 1)
            section, key = key.split(".", 1)
            settings[section][key] = value
        if not args.dry_run:
            provenance = validate_dataset(ROOT / settings["bc"]["data"], settings, args.mode)
            output = ROOT / settings["bc"]["output"]
            assert not output.exists(), "refusing to overwrite a checkpoint"
            output.parent.mkdir(parents=True, exist_ok=True)
            print(shlex.join(command), flush=True)
            result = subprocess.run(command, cwd=ROOT)
            if result.returncode == 0:
                contract = dict(CONTRACT, hidden=settings.getint("policy", "hidden_size"),
                    layers=settings.getint("policy", "num_layers"),
                    parameters=output.stat().st_size // 4)
                provenance.update({"contract": contract, "mode": mode,
                    "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                    "command": command, "offline_optimizer": "Adam"})
                output.with_suffix(".json").write_text(json.dumps(provenance, indent=2) + "\n")
            return result.returncode
    print(shlex.join(command), flush=True)
    if not args.dry_run:
        return subprocess.run(command, cwd=ROOT).returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
