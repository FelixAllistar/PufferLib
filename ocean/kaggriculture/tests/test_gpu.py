"""Explicitly opt-in native GPU qualification; no implicit GPU use or builds."""

from array import array
import math
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture(scope="module")
def binary():
    name = os.environ.get("KAGGRICULTURE_GPU_TEST_BINARY")
    if name is None:
        pytest.skip("set KAGGRICULTURE_GPU_TEST_BINARY on an idle GPU")
    executable = Path(name).resolve()
    assert executable.is_file()
    return executable


@pytest.mark.parametrize("agents,seat", [(2, 0), (1, 0), (1, 1)])
@pytest.mark.parametrize("split", [0, 1])
@pytest.mark.parametrize("graphs", [0, 1])
def test_prefix_masks_rng_and_stock_ppo(binary, agents, seat, split, graphs):
    result = subprocess.run([str(binary), "sampler", str(agents), str(seat),
        str(split), str(graphs)], cwd=ROOT, text=True, capture_output=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "sampler/PPO PASS" in result.stdout


@pytest.mark.parametrize("agents,seat,bot", [(2, 0, 0), (1, 0, 0), (1, 0, 1), (1, 1, 1)])
@pytest.mark.parametrize("graphs", [0, 1])
def test_gpu_adapter_matches_cpu_rules_and_controller(binary, agents, seat, bot, graphs):
    result = subprocess.run([str(binary), "adapter", str(agents), str(seat),
        str(bot), str(graphs)], cwd=ROOT, text=True, capture_output=True, timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "adapter PASS" in result.stdout


@pytest.mark.parametrize("async_mode,graphs", [(0, -1), (0, 1), (1, -1), (1, 1)])
def test_native_train_warm_start_and_update(tmp_path, async_mode, graphs):
    name = os.environ.get("KAGGRICULTURE_TRAIN_TEST_BINARY")
    if name is None:
        pytest.skip("set KAGGRICULTURE_TRAIN_TEST_BINARY on an idle GPU")
    executable = Path(name).resolve()
    assert executable.is_file()
    options = {
        "base.checkpoint_dir": tmp_path / "checkpoints",
        "base.log_dir": tmp_path / "logs",
        "base.load_model_path": "None",
        "base.async": async_mode,
        "base.cudagraphs": graphs,
        "base.eval_episodes": 0,
        "base.seed": 73,
        "selfplay.enabled": 0,
        "selfplay.eval_bot_games": 0,
        "selfplay.eval_games": 0,
        "vec.total_agents": 16,
        "vec.num_buffers": 1,
        "vec.num_policies": 1,
        "env.num_agents": 2,
        "policy.hidden_size": 32,
        "policy.num_layers": 1,
        "train.gpus": 1,
        "train.horizon": 16,
        "train.minibatch_size": 256,
        "train.replay_ratio": 1,
        "train.learning_rate": 0.001,
        "train.anneal_lr": 0,
        "sweep.downsample": 1,
    }

    def train(run, steps, load=None, lr=0.001):
        settings = dict(options, **{
            "base.run_id": run,
            "base.load_model_path": "None" if load is None else load,
            "train.total_timesteps": steps,
            "train.learning_rate": lr,
        })
        # Inherit the terminal so qualification is visible in the normal dashboard.
        result = subprocess.run([str(executable), "train"] +
            [f"--{key}={value}" for key, value in settings.items()],
            cwd=ROOT, timeout=180)
        assert result.returncode == 0
        directory = tmp_path / "checkpoints/kaggriculture" / run
        checkpoints = sorted(directory.glob("*.bin"))
        assert checkpoints
        data = checkpoints[-1].read_bytes()
        weights = array("f", data)
        assert weights and all(math.isfinite(w) for w in weights)
        return checkpoints[-1], data

    source, original = train("source", 256, lr=0)
    _, loaded = train("loaded", 512, load=source, lr=0)
    assert loaded == original
    _, updated = train("updated", 12288, load=source)
    assert len(updated) == len(original) and updated != original
    assert source.read_bytes() == original
