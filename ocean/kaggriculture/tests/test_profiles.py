from pathlib import Path
import shlex
import subprocess
import sys

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
    if profile == "terminal":
        for key in ["growth_land", "growth_crop", "growth_animal", "alive_daily", "quality_scale"]:
            assert f"--env.reward_{key}=0" in command
    else:
        assert "--env.reward_growth_animal=3.10535836" in command
