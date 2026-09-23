from pathlib import Path
import shlex
import subprocess
import sys
import configparser
import importlib.util
import json
import struct

import pytest


@pytest.mark.parametrize("profile", ["terminal", "shaped"])
@pytest.mark.parametrize("mode", ["train", "eval", "match", "sweep"])
def test_profile_uses_native_overrides(profile, mode):
    script = Path(__file__).resolve().parents[1] / "run.py"
    result = subprocess.run([sys.executable, str(script), mode, "--profile", profile,
        "--dry-run", "--train.total_timesteps=12345"], text=True, capture_output=True, check=True)
    command = shlex.split(result.stdout)
    assert command[1] == mode
    assert command[-1] == "--train.total_timesteps=12345"
    assert "--train.reward_clip=0" in command
    assert "--train.gamma=0.999817491" in command
    suffix = "initial_bc_critic.bin" if profile == "terminal" else "initial_bc.bin"
    assert f"--base.load_model_path=saved/kaggriculture/{suffix}" in command
    assert ("--env.reset_state_prob=0" in command) == (mode in ("eval", "match"))
    assert ("--headless" in command) == (mode in ("eval", "match"))
    if profile == "terminal":
        for key in ["growth_land", "growth_crop", "growth_animal", "alive_daily", "quality_scale"]:
            assert f"--env.reward_{key}=0" in command
    else:
        assert "--env.reward_growth_animal=3.10535836" in command


@pytest.mark.parametrize("mode", ["bc", "critic", "bc-critic"])
def test_offline_profiles_are_separate_native_program(mode):
    script = Path(__file__).resolve().parents[1] / "run.py"
    result = subprocess.run([sys.executable, str(script), mode, "--dry-run",
        "--bc.epochs=1"], text=True, capture_output=True, check=True)
    command = shlex.split(result.stdout)
    assert command[0].endswith("/build/kag_bc")
    assert "--bc.epochs=1" in command
    expected = {"bc": "actor", "critic": "critic", "bc-critic": "joint"}[mode]
    assert command[-1] == f"--bc.mode={expected}"


def test_offline_metadata_rejects_reward_and_controller_mismatch(tmp_path):
    script = Path(__file__).resolve().parents[1] / "run.py"
    spec = importlib.util.spec_from_file_location("kag_runner_metadata", script)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    root = script.parents[2]
    config = configparser.ConfigParser(interpolation=None)
    config.read([root / "config/default.ini", root / "config/kaggriculture.ini",
        script.parent / "profiles/terminal.ini"])
    source = configparser.ConfigParser(interpolation=None)
    source.read_dict({section: dict(config[section]) for section in config.sections()})
    source["env"]["reward_money_scale"] = source["env"].pop("reward_money")
    source["env"]["policy_market_slots"] = source["env"].pop("market_slots")
    source["env"]["policy_max_hands"] = source["env"].pop("max_hands")
    source["env"]["reward_money_timing"] = "0"
    source["env"]["reward_pbrs_scale"] = "0"
    import io
    profile = io.StringIO()
    source.write(profile)
    path = tmp_path / "data.bc"
    path.write_bytes(struct.pack("<16I2Qd", 0x4b414742, 3, 2160, 1424, 47, 248,
        3, 720, 4, 3, 5, 2, 2, 1, 0, 1, 123, 456, 0.999817491))
    path.with_suffix(".json").write_text(json.dumps({"format": "kaggriculture_entity_bc_v3",
        "teacher": "test", "source_hash": f"{123:016x}", "semantics_hash": f"{456:016x}",
        "train_games": 2, "validation_games": 1, "profile": profile.getvalue()}))
    assert runner.validate_dataset(path, config, "critic")["train_games"] == 2
    config["env"]["reward_money"] = "9"
    with pytest.raises(AssertionError, match="reward mismatch"):
        runner.validate_dataset(path, config, "critic")
    runner.validate_dataset(path, config, "bc")  # actor labels do not depend on reward scale
    config["env"]["max_hands"] = "5"
    with pytest.raises(AssertionError, match="controller mismatch"):
        runner.validate_dataset(path, config, "bc")
