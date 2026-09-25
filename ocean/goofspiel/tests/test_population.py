"""Population launcher contracts; optional real native GPU training."""
import configparser
import json
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "ocean/goofspiel/train_population.sh"


@pytest.fixture
def population(tmp_path):
    binary = tmp_path / "trainer"
    binary.write_text("""#!/usr/bin/env python3
import json, os, sys
with open(os.environ['CALLS'], 'a') as out:
    out.write(json.dumps(sys.argv[1:]) + '\\n')
sys.exit(int(os.environ.get('FAIL', 0)))
""")
    binary.chmod(0o755)
    return dict(os.environ, GOOFSPIEL_TRAIN_BINARY=str(binary),
        GOOFSPIEL_CHECKPOINT_DIR=str(tmp_path / "checkpoints"),
        CALLS=str(tmp_path / "calls"))


def launch(env, *args):
    return subprocess.run(["bash", str(SCRIPT), *args], cwd=ROOT,
        env=env, text=True, capture_output=True, timeout=120)


def test_native_arguments_and_overrides(population):
    result = launch(population, "2", "1001", "example",
        "--train.total_timesteps=1024", "--policy.hidden_size=32")
    assert result.returncode == 0, result.stdout + result.stderr
    calls = [json.loads(line) for line in Path(population["CALLS"]).read_text().splitlines()]
    assert len(calls) == 2
    for index, call in enumerate(calls):
        assert call[0] == "train"
        assert all(arg.startswith("--") and "=" in arg for arg in call[1:])
        settings = dict(arg[2:].split("=", 1) for arg in call[1:])
        seed = str(1001 + index)
        assert settings["base.seed"] == settings["env.seed"] == seed
        assert settings["base.run_id"] == "example_seed" + seed
        assert settings["base.load_model_path"] == "None"
        assert settings["train.total_timesteps"] == "1024"
        assert settings["policy.hidden_size"] == "32"
        assert settings["env.exact_exploiter"] == "1"
        assert not any("emag" in key or "frozen_bank" in key for key in settings)
    assert "Population 2/2" in result.stdout


def test_collision_checked_before_first_run(population):
    existing = Path(population["GOOFSPIEL_CHECKPOINT_DIR"]) / "goofspiel/example_seed1002"
    existing.mkdir(parents=True)
    result = launch(population, "2", "1001", "example")
    assert result.returncode != 0
    assert "Run already exists" in result.stderr
    assert not Path(population["CALLS"]).exists()


def test_failed_member_stops_population(population):
    result = launch(dict(population, FAIL="9"), "2", "1001", "example")
    assert result.returncode == 9
    assert len(Path(population["CALLS"]).read_text().splitlines()) == 1


@pytest.mark.parametrize("setting", ["base.seed", "env.seed", "base.run_id",
    "base.checkpoint_dir", "base.load_model_path"])
def test_owned_setting_rejected(population, setting):
    result = launch(population, "2", "1001", "example", f"--{setting}=unexpected")
    assert result.returncode == 2
    assert not Path(population["CALLS"]).exists()


@pytest.mark.skipif(not os.environ.get("GOOFSPIEL_TRAIN_BINARY"),
    reason="requires idle GPU and native Goofspiel trainer")
def test_native_population(tmp_path):
    env = dict(os.environ,
        GOOFSPIEL_TRAIN_BINARY=str(Path(os.environ["GOOFSPIEL_TRAIN_BINARY"]).resolve()),
        GOOFSPIEL_CHECKPOINT_DIR=str(tmp_path / "checkpoints"))
    result = launch(env, "2", "1001", "native",
        f"--base.log_dir={tmp_path / 'logs'}", "--base.checkpoint_interval=1",
        "--base.cudagraphs=1", "--base.async=0", "--vec.total_agents=64",
        "--vec.num_threads=1", "--vec.num_buffers=1",
        "--train.total_timesteps=1024", "--train.horizon=8",
        "--train.minibatch_size=64", "--train.gpus=1",
        "--env.exact_exploiter_history=3", "--selfplay.eval_games=0")
    assert result.returncode == 0, result.stdout + result.stderr
    starts = []
    for seed in (1001, 1002):
        run = f"native_seed{seed}"
        config = configparser.ConfigParser()
        config.read(tmp_path / "logs/goofspiel" / f"{run}.ini")
        assert config.getint("policy", "hidden_size") == 64
        assert config.getint("policy", "num_layers") == 1
        assert config.getint("vec", "hist_policy_hidden_size") == 64
        assert config.getint("vec", "hist_policy_num_layers") == 1
        assert config.getint("base", "seed") == seed
        checkpoints = sorted((tmp_path / "checkpoints/goofspiel" / run).glob("*.bin"))
        assert int(checkpoints[-1].stem) >= 1024
        assert checkpoints[0].read_bytes() != checkpoints[-1].read_bytes()
        assert all(Path(str(path) + ".exact").is_file() for path in checkpoints)
        starts.append(checkpoints[0].read_bytes())
    assert starts[0] != starts[1]
