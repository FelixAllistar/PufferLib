#!/usr/bin/env python3
"""PROTEIN worker: train -> exit/release GPU -> fixed fresh-start evaluation.

No alternate config files or Python ML dependencies. The native parent passes
the complete resolved INI as arguments. Results use src/pufferl.cu's TrainResult
ABI; the trainer never inherits that pipe, so a failed evaluation cannot be
mistaken for a successful trial scored from training self-play.
"""
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
RESULT = struct.Struct("=fffi192f")  # score, cost, steps, points, 3 x float[64]
EVAL = re.compile(r"CUDA_EVAL env=kaggriculture score=(\S+) perf=(\S+) games=(\d+) params=(\d+) draw=(\S+)")


def arguments(argv):
    if not argv or argv[0] != "train":
        raise ValueError("this is a native sweep worker, not a standalone training launcher")
    values = {}
    for arg in argv[1:]:
        if not arg.startswith("--") or "=" not in arg:
            raise ValueError(f"invalid native argument: {arg}")
        key, value = arg[2:].split("=", 1)
        values[key] = value
    return values


def command(binary, mode, values, overrides):
    merged = dict(values, **overrides)
    # The final result belongs to this worker, never to a nested trainer.
    merged["base.result_fd"] = "0"
    args = [str(binary), mode]
    if mode == "eval":
        args.append("--headless")
    return args + [f"--{key}={value}" for key, value in merged.items()]


def eval_overrides(values, checkpoint, seat):
    return {
        "base.load_model_path": str(checkpoint),
        "base.teacher_model_path": "None",
        "base.load_enemy_model_path": "None",
        "base.seed": values["sweep.eval_seed"],
        "base.eval_episodes": values["sweep.eval_games_per_seat"],
        "base.eval_agents": values["sweep.eval_agents"],
        "vec.total_agents": values["sweep.eval_agents"],
        "vec.num_buffers": "1", "vec.num_threads": "4",
        "vec.num_policies": "1", "vec.hist_policy_percent": "0",
        "selfplay.enabled": "0", "selfplay.eval_pool_size": "0",
        "selfplay.eval_games": "0", "selfplay.eval_bot_games": "0",
        "env.num_agents": "1", "env.num_bots": "1",
        "env.learner_seat": str(seat), "env.bot_policy": "1",
        "env.reset_state_prob": "0", "env.reset_state_bank": "None",
        "env.opponent_noise_initial": "0", "env.opponent_noise_final": "0",
        "train.teacher_kl_coefficient": "0", "train.verb_eps": "0",
        "sweep.metric": "root_money",
    }


def parse_eval(output, minimum_games):
    matches = list(EVAL.finditer(output))
    if len(matches) != 1:
        raise ValueError("evaluation did not produce exactly one CUDA_EVAL result")
    score, perf, games, params, draw = matches[0].groups()
    result = dict(root_money=float(score), win_rate=float(perf), games=int(games),
                  parameters=int(params), draw_rate=float(draw))
    if (not all(math.isfinite(result[k]) for k in ("root_money", "win_rate", "draw_rate"))
            or result["games"] < minimum_games or not 0 <= result["win_rate"] <= 1):
        raise ValueError(f"invalid evaluation result: {result}")
    return result


def result_packet(score, cost, steps):
    if not all(math.isfinite(x) for x in (score, cost, steps)) or cost <= 0 or steps <= 0:
        raise ValueError("invalid sweep result")
    return RESULT.pack(score, cost, steps, 1,
                       score, *([0.0] * 63), cost, *([0.0] * 63), steps, *([0.0] * 63))


def run_logged(cmd, path):
    with path.open("x") as stream:
        print(json.dumps(cmd), file=stream, flush=True)
        process = subprocess.Popen(cmd, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = process.wait()
        except BaseException:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            raise
    if code:
        raise subprocess.CalledProcessError(code, cmd)


def run_trial(values):
    fd = int(values["base.result_fd"])
    if fd <= 2:
        raise ValueError("sweep worker requires the parent's result pipe")
    os.set_inheritable(fd, False)
    binary = ROOT / values["sweep.trainer_path"]
    run_id = values["base.run_id"]
    if not re.fullmatch(r"sweep_[0-9]+_[0-9]+", run_id):
        raise ValueError("invalid native sweep run ID")
    directory = ROOT / values["base.log_dir"] / "kaggriculture" / run_id
    directory.mkdir(parents=True, exist_ok=False)
    receipt = dict(status="running", parameters=values, started=time.time())
    (directory / "inputs.json").write_text(json.dumps(receipt, indent=2))
    started = time.monotonic()
    try:
        initial = ROOT / values["base.load_model_path"]
        if not initial.is_file():
            raise ValueError(f"missing BC checkpoint: {initial}")
        batch = int(values["vec.total_agents"]) * int(values["train.horizon"])
        steps = int(float(values["train.total_timesteps"])) // batch * batch
        if steps < batch:
            raise ValueError("trial budget must contain a complete rollout")
        checkpoint = (ROOT / values["base.checkpoint_dir"] / "kaggriculture"
                      / run_id / f"{steps:016d}.bin")
        if checkpoint.parent.exists():
            raise ValueError(f"checkpoint directory already exists: {checkpoint.parent}")
        train = command(binary, "train", values, {
            "base.eval_episodes": "0", "selfplay.eval_games": "0",
            "selfplay.eval_bot_games": "0", "selfplay.eval_pool_size": "0",
        })
        run_logged(train, directory / "train.log")
        if not checkpoint.is_file() or checkpoint.stat().st_size != initial.stat().st_size:
            raise ValueError(f"missing or wrong-size final checkpoint: {checkpoint}")
        # train has exited, including its CUDA context. Never run eval alongside it.
        evaluations = []
        for seat in (0, 1):
            path = directory / f"eval_seat{seat}.log"
            cmd = command(binary, "eval", values, eval_overrides(values, checkpoint, seat))
            run_logged(cmd, path)
            evaluations.append(parse_eval(path.read_text(), int(values["sweep.eval_games_per_seat"])))
        score = sum(e["root_money"] for e in evaluations) / 2
        cost = time.monotonic() - started
        receipt.update(status="complete", checkpoint=str(checkpoint), score=score,
                       win_rate=sum(e["win_rate"] for e in evaluations) / 2,
                       seconds=cost, steps=steps, evaluations=evaluations)
        packet = result_packet(score, cost, steps)
        (directory / "result.json").write_text(json.dumps(receipt, indent=2))
        if os.write(fd, packet) != len(packet):
            raise OSError("short write to sweep result pipe")
    except BaseException as error:
        receipt.update(status="failed", error=str(error), seconds=time.monotonic() - started)
        (directory / "failure.json").write_text(json.dumps(receipt, indent=2))
        raise
    finally:
        os.close(fd)


if __name__ == "__main__":
    run_trial(arguments(sys.argv[1:]))
