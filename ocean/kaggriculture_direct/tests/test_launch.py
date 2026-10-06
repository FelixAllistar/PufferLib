import configparser
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]


def command(*args):
    p = subprocess.run([sys.executable,"ocean/kaggriculture/run.py",*args,"--dry-run"],
                       cwd=ROOT,text=True,capture_output=True)
    assert p.returncode == 0, p.stderr
    return [shlex.split(line) for line in p.stdout.splitlines()]


def test_one_config():
    assert not (ROOT/"config/kaggriculture_direct.ini").exists()
    c=configparser.ConfigParser()
    c.read(ROOT/"config/kaggriculture.ini")
    assert c.getint("policy","action_version") == 7
    assert c.getfloat("env","reset_state_prob") == .9
    assert "/compact_v7/" in c.get("base","load_model_path")
    assert c.getint("train","horizon") == 720
    assert c.getfloat("train","gamma") == 1
    assert c.getfloat("train","gae_lambda") == .97
    assert c.getint("train","replay_ratio") == 1
    assert c.getint("selfplay","enabled") == 0
    assert c.getint("vec","num_policies") == 1
    assert c.getint("env","num_agents") == 2
    assert c.getint("env","max_hands") == 19
    assert c.getint("env","reward_win_loss_draw") == 1
    assert c.getint("policy","critic_mode") == 2
    for key in ("reward_growth_land","reward_growth_crop","reward_growth_animal",
                "potential_beta","opponent_noise_initial","opponent_noise_final"):
        assert c.getfloat("env",key) == 0
    assert command("train") == [[str(ROOT/"puffer"),"train"]]


def test_six_fresh_bc_shapes_no_config_forks():
    commands=command("bc-grid")
    assert len(commands) == 6
    outputs=set()
    for command_,(h,l) in zip(commands,[(h,l) for h in (256,512,1024) for l in (2,3)]):
        assert f"--policy.hidden_size={h}" in command_
        assert f"--policy.num_layers={l}" in command_
        assert "--base.load_model_path=None" in command_
        assert "--bc.mode=actor" in command_
        outputs.add(next(x for x in command_ if x.startswith("--bc.output=")))
    assert len(outputs) == 6


def test_model_selection_and_fresh_evaluation():
    options=command("eval","--hidden","1024","--layers","3")[0]
    assert "--env.reset_state_prob=0" in options
    assert "--policy.hidden_size=1024" in options and "--policy.num_layers=3" in options
    assert any(x.endswith("/h1024_l3/bc.bin") for x in options)


def test_legacy_profile_rejected():
    p=subprocess.run([sys.executable,"ocean/kaggriculture/run.py","train","--profile","wld_paired","--dry-run"],
                     cwd=ROOT,text=True,capture_output=True)
    assert p.returncode != 0 and "overlays are retired" in p.stderr
