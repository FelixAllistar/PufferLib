"""Opt-in GPU-simulator training smoke; requires an idle GPU."""
import configparser
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.mark.skipif(not os.environ.get("GOOFSPIEL_TRAIN_BINARY"),
    reason="requires native Goofspiel GPU trainer")
@pytest.mark.parametrize("graphs", [-1, 1])
def test_frozen_bank_training(tmp_path, graphs):
    binary = Path(os.environ["GOOFSPIEL_TRAIN_BINARY"]).resolve()
    settings = {
        "base.run_id": "smoke", "base.checkpoint_dir": tmp_path,
        "base.log_dir": tmp_path / "logs", "base.eval_episodes": 0,
        "base.checkpoint_interval": 0, "base.cudagraphs": graphs,
        "base.load_model_path": "None", "base.async": 0,
        "vec.total_agents": 64, "vec.num_buffers": 1, "vec.num_threads": 1,
        "vec.num_policies": 5, "vec.hist_policy_percent": .5,
        "vec.hist_policy_hidden_size": 32, "vec.hist_policy_num_layers": 2,
        "selfplay.enabled": 1, "selfplay.initial_opponents": "None",
        "selfplay.eval_games": 0,
        "policy.hidden_size": 32, "policy.num_layers": 2,
        "train.total_timesteps": 4096, "train.horizon": 16,
        "train.minibatch_size": 256, "train.gpus": 1,
    }
    result = subprocess.run([str(binary), "train"] +
        [f"--{key}={value}" for key, value in settings.items()],
        cwd=ROOT, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    saved = configparser.ConfigParser()
    saved.read(tmp_path / "logs" / "goofspiel" / "smoke.ini")
    assert saved.getint("vec", "num_policies") == 5
    assert saved.getint("selfplay", "enabled") == 1
    checkpoints = sorted((tmp_path / "goofspiel" / "smoke").glob("*.bin"))
    assert checkpoints, result.stdout
    assert int(checkpoints[-1].stem) >= 4096
    assert checkpoints[-1].stat().st_size > 0
    assert checkpoints[0].read_bytes() != checkpoints[-1].read_bytes()
