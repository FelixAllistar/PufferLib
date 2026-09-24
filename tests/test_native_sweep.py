"""Opt-in native fixed-dimension sweep tests on an idle GPU.

Build: CUDA_HOME=/usr/local/cuda bash build.sh bomberman build/test_native_sweep --float
Run: PUFFER_SWEEP_TEST_BINARY=build/test_native_sweep uv run --no-project \
    --with pytest python -m pytest -q tests/test_native_sweep.py

Use --float on pre-Ampere hardware. Tests write only under pytest's temporary
directory, disable core dumps, and do not modify production configuration.
"""

import configparser
import os
from pathlib import Path
import resource
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def binary():
    path = os.environ.get("PUFFER_SWEEP_TEST_BINARY")
    if path is None:
        pytest.skip("set PUFFER_SWEEP_TEST_BINARY to opt into native tests")
    executable = Path(path).resolve()
    assert executable.is_file()
    return executable


def run(binary, mode, settings):
    def no_core_dump():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    return subprocess.run([str(binary), mode] + [f"--{k}={v}" for k, v in settings.items()],
        cwd=ROOT, capture_output=True, text=True, timeout=120, preexec_fn=no_core_dump)


def settings(directory):
    config = configparser.ConfigParser(interpolation=None)
    config.read([ROOT / "config/default.ini", ROOT / "config/bomberman.ini"])
    options = {
        "base.run_id": "source", "base.checkpoint_dir": directory / "checkpoints",
        "base.log_dir": directory / "logs", "base.load_model_path": "None",
        "base.eval_episodes": 0, "base.async": 0, "base.cudagraphs": -1,
        "base.checkpoint_interval": 0, "selfplay.enabled": 0,
        "selfplay.eval_games": 0, "selfplay.eval_bot_games": 0,
        "vec.total_agents": 32, "vec.num_buffers": 1, "vec.num_threads": 1,
        "vec.num_policies": 2, "vec.hist_policy_percent": 1,
        "policy.hidden_size": 32, "policy.num_layers": 1,
        "train.gpus": 1, "train.total_timesteps": 512, "train.horizon": 8,
        "train.minibatch_size": 64, "train.learning_rate": 0,
        "train.anneal_lr": 0, "train.anneal_ent_coef": 0,
        "train.ent_coef": 0, "env.max_ticks": 8,
        "sweep.max_runs": 2, "sweep.gpus": 1, "sweep.downsample": 1,
    }
    for section in config.sections():
        if section.startswith("sweep."):
            target_section, key = section[6:].rsplit(".", 1)
            value = options.get(section[6:], config[target_section][key])
            options[f"{section}.min"] = value
            options[f"{section}.max"] = value
    return options


def test_fixed_dimensions_preserve_checkpoint_architecture_and_budget(binary, tmp_path):
    options = settings(tmp_path)
    source = run(binary, "train", options | {
        "train.total_timesteps": 0, "selfplay.enabled": 1})
    assert source.returncode == 0, source.stdout + source.stderr
    checkpoint = tmp_path / "checkpoints/bomberman/source/0000000000000000.bin"
    expected = checkpoint.read_bytes()
    # Zero LR isolates loading. Entropy remains searchable, all other dimensions
    # are fixed, including the inherited scale=time training-budget dimension.
    sweep = run(binary, "sweep", options | {
        "base.load_model_path": checkpoint,
        "sweep.train.ent_coef.distribution": "uniform",
        "sweep.train.ent_coef.min": 0, "sweep.train.ent_coef.max": 0.01})
    assert sweep.returncode == 0, sweep.stdout + sweep.stderr
    assert "sweep run=0 " in sweep.stdout and "sweep run=1 " in sweep.stdout
    assert sweep.stdout.count("steps=512 ") == 2, sweep.stdout
    completed = list((tmp_path / "checkpoints/bomberman").glob("sweep_*/0000000000000512.bin"))
    assert len(completed) == 2
    assert all(path.read_bytes() == expected for path in completed)


def test_fixed_dimension_must_match_config(binary, tmp_path):
    result = run(binary, "sweep", settings(tmp_path) | {
        "sweep.policy.hidden_size.min": 64, "sweep.policy.hidden_size.max": 64})
    assert result.returncode != 0
    assert "fixed sweep range must match config" in result.stderr


def test_all_fixed_is_not_a_search(binary, tmp_path):
    result = run(binary, "sweep", settings(tmp_path))
    assert result.returncode != 0
    assert "sweep requires at least one varying parameter" in result.stderr
