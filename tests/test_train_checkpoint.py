"""Opt-in CUDA integration tests against the native trainer, not Python PPO.

Build with the stock CPU-env recipe (use --float on pre-Ampere GPUs):
    bash build.sh chain_reaction build/test_train_checkpoint --float
Run from the repository root on an idle GPU:
    PUFFER_CHECKPOINT_TEST_BINARY=build/test_train_checkpoint \
        uv run --no-project --with pytest python -m pytest -q \
        tests/test_train_checkpoint.py

Repeat without --float on a BF16-capable GPU. No build or GPU work occurs
unless the binary is explicitly supplied.
"""

from array import array
import math
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def binary():
    path = os.environ.get("PUFFER_CHECKPOINT_TEST_BINARY")
    if path is None:
        pytest.skip("set PUFFER_CHECKPOINT_TEST_BINARY to opt into GPU tests")
    executable = Path(path).resolve()
    assert executable.is_file(), executable
    return executable


def train(binary, directory, run_id, **settings):
    options = {
        "base.run_id": run_id,
        "base.checkpoint_dir": directory / "checkpoints",
        "base.log_dir": directory / "logs",
        "base.seed": 73,
        "base.async": 0,
        "base.cudagraphs": -1,
        "base.eval_episodes": 0,
        "base.load_model_path": "None",
        "policy.hidden_size": 32,
        "policy.num_layers": 1,
        "vec.total_agents": 32,
        "vec.num_buffers": 1,
        "vec.num_threads": 1,
        "vec.num_policies": 2,
        "vec.hist_policy_percent": 0.5,
        "env.num_agents": 2,
        "selfplay.enabled": 1,
        "selfplay.eval_games": 0,
        "selfplay.eval_bot_games": 0,
        "train.gpus": 1,
        "train.horizon": 8,
        "train.minibatch_size": 64,
        "train.replay_ratio": 1,
        "train.total_timesteps": 0,
        "train.learning_rate": 0,
        "train.anneal_lr": 0,
        "sweep.downsample": 1,
    }
    options.update(settings)
    result = subprocess.run(
        [str(binary), "train"] + [f"--{k}={v}" for k, v in options.items()],
        cwd=ROOT, text=True, capture_output=True, timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return directory / "checkpoints" / "chain_reaction" / run_id


@pytest.fixture(scope="module")
def source(binary, tmp_path_factory):
    directory = tmp_path_factory.mktemp("checkpoint_source")
    run = train(binary, directory, "source", **{"base.seed": 123})
    checkpoint = run / "0000000000000000.bin"
    assert checkpoint.stat().st_size > 0
    return checkpoint


def test_no_checkpoint_preserves_seeded_initialization(binary, source, tmp_path):
    first = train(binary, tmp_path, "first") / "0000000000000000.bin"
    second = train(binary, tmp_path, "second") / "0000000000000000.bin"
    assert first.read_bytes() == second.read_bytes()
    assert first.read_bytes() != source.read_bytes()


@pytest.mark.parametrize("async_mode", [0, 1])
@pytest.mark.parametrize("graphs", [-1, 1])
@pytest.mark.parametrize("selfplay", [0, 1])
@pytest.mark.parametrize("load_latest", [False, True])
def test_train_loads_weights_before_pool_and_updates(
        binary, source, tmp_path, async_mode, graphs, selfplay, load_latest):
    expected = source.read_bytes()
    checkpoint = tmp_path / "checkpoints" / "chain_reaction" / "source.bin"
    checkpoint.parent.mkdir(parents=True)
    checkpoint.write_bytes(expected)
    run = train(binary, tmp_path, "warm", **{
        "base.async": async_mode,
        "base.cudagraphs": graphs,
        "base.load_model_path": "latest" if load_latest else checkpoint,
        "selfplay.enabled": selfplay,
        "train.total_timesteps": 512,
    })
    if selfplay:
        assert (run / "0000000000000000.bin").read_bytes() == expected
    # Zero LR isolates loading from optimization; two epochs exercise prefetch.
    assert (run / "0000000000000512.bin").read_bytes() == expected
    assert checkpoint.read_bytes() == expected


@pytest.mark.parametrize("graphs", [-1, 1])
def test_first_async_update_matches_sync(binary, source, tmp_path, graphs):
    outputs = []
    for async_mode in (0, 1):
        run = train(binary, tmp_path, f"actor_{async_mode}", **{
            "base.async": async_mode,
            "base.cudagraphs": graphs,
            "base.load_model_path": source,
            "selfplay.enabled": 0,
            "train.total_timesteps": 256,
            "train.learning_rate": 0.001,
        })
        output = (run / "0000000000000256.bin").read_bytes()
        assert output != source.read_bytes(), "expected a real PPO update"
        outputs.append(array("f", output))
    assert len(outputs[0]) == len(outputs[1])
    assert all(math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-7)
        for a, b in zip(*outputs)), "async first rollout used different weights"
