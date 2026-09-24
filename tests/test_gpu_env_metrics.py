"""Opt-in native GPU logging regressions; run on an idle GPU.

Set PUFFER_TRIANGLEPATH_GPU_BINARY and/or PUFFER_SURVIVORS_GPU_BINARY to
their --cu trainer builds. Use --float on pre-Ampere hardware.
"""

import os
from pathlib import Path
import re
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("environment,variable", [
    ("trianglepath", "PUFFER_TRIANGLEPATH_GPU_BINARY"),
    ("puffer_survivors", "PUFFER_SURVIVORS_GPU_BINARY"),
])
@pytest.mark.parametrize("graphs", [-1, 0])
def test_gpu_trainer_reports_completed_episodes(environment, variable, graphs, tmp_path):
    binary = os.environ.get(variable)
    if binary is None:
        pytest.skip(f"set {variable} to enable native GPU tests")
    command = [str(Path(binary).resolve()), "train",
        "--base.run_id=metrics", f"--base.checkpoint_dir={tmp_path / 'checkpoints'}",
        f"--base.log_dir={tmp_path / 'logs'}", "--base.load_model_path=None",
        "--base.eval_episodes=0", "--base.checkpoint_interval=0",
        f"--base.cudagraphs={graphs}", "--base.async=1",
        "--vec.total_agents=32", "--vec.num_buffers=1", "--vec.num_threads=1",
        "--vec.num_policies=1", "--selfplay.enabled=0", "--train.gpus=1",
        "--policy.hidden_size=32", "--policy.num_layers=1",
        "--train.total_timesteps=2048", "--train.horizon=16",
        "--train.minibatch_size=128"]
    if environment == "puffer_survivors":
        command += ["--env.max_steps=16", "--env.player_health=1000000"]
    else:
        command += ["--env.height=6", "--env.cell_min=1", "--env.cell_max=9"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    perf = [float(value) for value in re.findall(r"\bperf\s+([0-9.]+)", result.stdout)]
    assert perf, result.stdout  # Simulation-only tests cannot detect a broken log reducer.
    assert all(0 < value <= 1 for value in perf), perf
    if environment == "puffer_survivors":
        assert all(value == 1 for value in perf), perf
