"""Opt-in native Pokémon expert-bank integration on an idle GPU."""
import configparser
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[3]


@pytest.mark.skipif(not os.environ.get("POKEMON_TRAIN_BINARY"), reason="requires native GPU binary")
@pytest.mark.parametrize("graphs", [-1, 1])
@pytest.mark.parametrize("learner_arch", [(8, 1), (64, 2)])
def test_named_experts(tmp_path, graphs, learner_arch):
    binary = Path(os.environ["POKEMON_TRAIN_BINARY"]).resolve()

    def train(name, **overrides):
        settings = {
            "base.run_id": name, "base.checkpoint_dir": tmp_path,
            "base.log_dir": tmp_path / "logs", "base.eval_episodes": 0,
            "base.checkpoint_interval": 0, "base.cudagraphs": graphs,
            "env.force_core_combos": 0, "env.reset_state_prob": 0,
            "env.native_league": "None", "selfplay.enabled": 0,
            "vec.total_agents": 16, "vec.num_threads": 2,
            "policy.hidden_size": 16, "policy.num_layers": 1,
            "train.horizon": 8, "train.minibatch_size": 32,
            "train.total_timesteps": 128,
        }
        settings.update(overrides)
        result = subprocess.run([str(binary), "train"] +
            [f"--{k}={v}" for k, v in settings.items()], cwd=ROOT,
            env={**os.environ, "PUFFER_POKEMON_AUDIT": "1"},
            capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stdout + result.stderr
        directory = tmp_path / "pokemon" / name
        return directory, result.stdout

    teams = ["species:65,128,143", "species:124,121,113"]
    leads = ["65", "124"]
    paths = []
    for i in range(2):
        directory, _ = train(f"expert{i}", **{
            "env.learner_team": teams[i], "env.learner_lead": leads[i]})
        paths.append(directory / "0000000000000128.bin")
    saved = configparser.ConfigParser()
    saved.read(paths[0].parent / "config.ini")
    opponents = tmp_path / "opponents.txt"
    opponents.write_text("".join(f"{p}\n" for p in paths))
    manifest = configparser.ConfigParser()
    manifest["native"] = dict(banks="2", hidden_size="16", num_layers="1",
        rules_sha=saved["env"]["rules_sha"], opponents=str(opponents))
    for i in range(2):
        manifest[f"bank.{i}"] = dict(path=str(paths[i]), team=teams[i], lead=leads[i])
    native = tmp_path / "native.ini"
    with native.open("w") as stream:
        manifest.write(stream)
    original = [p.read_bytes() for p in paths]
    directory, output = train("learner", **{
        "env.native_league": native, "env.expert_fraction": .5,
        "env.force_core_combos": 1, "env.core_pool": "1,2,3,4",
        "policy.hidden_size": learner_arch[0], "policy.num_layers": learner_arch[1],
        "train.total_timesteps": 2048})
    saved.read(directory / "config.ini")
    assert saved["vec"]["num_policies"] == "3"
    assert saved["vec"]["hist_policy_hidden_size"] == "16"
    assert int(saved["policy"]["hidden_size"]) == learner_arch[0]
    assert int(saved["policy"]["num_layers"]) == learner_arch[1]
    assert saved["selfplay"]["opp_timeout_steps"] == "0"
    assert "2 frozen bank slots" in output
    assert int(saved["env"]["core_next_drafted"]) > 0
    assert (directory / "0000000000002048.bin").is_file()
    assert original == [p.read_bytes() for p in paths]
