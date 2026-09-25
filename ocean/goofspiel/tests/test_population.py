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


@pytest.fixture
def evaluation(tmp_path):
    tool = tmp_path / "evaluate"
    tool.write_text("""#!/usr/bin/env python3
import os, sys
from pathlib import Path
if os.environ.get('FAIL'):
    sys.exit(9)
args = sys.argv[1:]
if args[0] == 'match':
    assert '--headless' in args
    settings = dict(arg[2:].split('=', 1) for arg in args[1:] if '=' in arg)
    a = int(Path(settings['base.load_model_path']).parent.name[-1])
    b = int(Path(settings['base.load_enemy_model_path']).parent.name[-1])
    score = .5 if a == b else .8 if (a, b) in ((1, 2), (2, 3), (3, 1)) else .2
    print(f'CUDA_EVAL env=goofspiel score={score} perf={score} games=32 params=1 draw=0.1')
elif args[0] == 'behavior':
    print('path_a\\tpath_b\\tjsd')
else:
    paths = [arg for arg in args if not arg.startswith('--')]
    if len(paths) == 1:
        print('nash_conv=0.2 exploitability=0.1')
        sys.exit(0)
    for path in paths:
        assert path.endswith('.bin')
        print(f'{.1 if Path(path).stem == "001" else .2}\\t{path}')
    print(f'best=0.1\\t{paths[-1]}')
""")
    tool.chmod(0o755)
    for seed in (1, 2, 3):
        run = tmp_path / "checkpoints/goofspiel" / f"sample_seed{seed}"
        run.mkdir(parents=True)
        for name in ("000.bin", "001.bin", "001.bin.emag"):
            (run / name).write_bytes(b"preserved")
    return dict(os.environ, GOOFSPIEL_TRAIN_BINARY=str(tool),
        GOOFSPIEL_EXACT_GPU=str(tool), GOOFSPIEL_CHECKPOINT_DIR=str(tmp_path / "checkpoints"),
        GOOFSPIEL_LOG_DIR=str(tmp_path / "reports"))


@pytest.mark.parametrize("mode", ["best", "all", "scan", "jsd"])
def test_population_evaluation(evaluation, mode):
    result = subprocess.run(["bash", str(ROOT / "ocean/goofspiel/eval_population.sh"),
        "sample", "32", "0.002", mode], cwd=ROOT, env=evaluation,
        text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    reports = Path(evaluation["GOOFSPIEL_LOG_DIR"]) / "goofspiel"
    if mode == "jsd":
        assert "jsd" in (reports / "sample_behavior.tsv").read_text()
        return
    suffix = "" if mode == "best" else "_all"
    manifest = (reports / f"sample{suffix}_manifest.tsv").read_text()
    assert len(manifest.splitlines()) == (4 if mode == "best" else 7)
    assert ".emag" not in manifest
    if mode == "best":
        assert "000.bin" not in manifest
        cycles = (reports / "sample_cycles.tsv").read_text()
        assert "sample_seed1\tsample_seed2\tsample_seed3\ta>b>c>a" in cycles
        rows = (reports / "sample_payoff.tsv").read_text().splitlines()
        assert rows[1].split("\t")[1:] == ["0.500000", "0.800000", "0.200000"]
    elif mode == "scan":
        assert not (reports / "sample_all_payoff.tsv").exists()


def test_population_evaluator_failure_propagates(evaluation):
    result = subprocess.run(["bash", str(ROOT / "ocean/goofspiel/eval_population.sh"),
        "sample", "32", "0.002", "best"], cwd=ROOT, env=dict(evaluation, FAIL="1"),
        text=True, capture_output=True, timeout=30)
    assert result.returncode == 9


def test_single_checkpoint_per_member(evaluation):
    for path in Path(evaluation["GOOFSPIEL_CHECKPOINT_DIR"]).glob("goofspiel/*/000.bin"):
        path.unlink()
    result = subprocess.run(["bash", str(ROOT / "ocean/goofspiel/eval_population.sh"),
        "sample", "32", "0.002", "best"], cwd=ROOT, env=evaluation,
        text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    manifest = Path(evaluation["GOOFSPIEL_LOG_DIR"]) / "goofspiel/sample_manifest.tsv"
    assert len(manifest.read_text().splitlines()) == 4


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
    snapshot_count = 0
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
        snapshot_count += len(checkpoints)
        assert int(checkpoints[-1].stem) >= 1024
        assert checkpoints[0].read_bytes() != checkpoints[-1].read_bytes()
        assert all(Path(str(path) + ".exact").is_file() for path in checkpoints)
        starts.append(checkpoints[0].read_bytes())
    assert starts[0] != starts[1]

    solver = os.environ.get("GOOFSPIEL_EXACT_GPU")
    if not solver:
        return
    env.update(GOOFSPIEL_EXACT_GPU=str(Path(solver).resolve()),
        GOOFSPIEL_LOG_DIR=str(tmp_path / "reports"))
    evaluator = ROOT / "ocean/goofspiel/eval_population.sh"
    for mode in ("best", "scan", "all", "jsd"):
        result = subprocess.run(["bash", str(evaluator), "native", "32", "0.002", mode,
            "--base.eval_agents=16", "--vec.num_threads=1", "--vec.num_buffers=1",
            "--train.horizon=8", "--base.cudagraphs=1"], cwd=ROOT, env=env,
            capture_output=True, text=True, timeout=180)
        assert result.returncode == 0, result.stdout + result.stderr
    reports = tmp_path / "reports/goofspiel"
    for suffix, count in (("", 2), ("_all", snapshot_count)):
        rows = (reports / f"native{suffix}_payoff.tsv").read_text().splitlines()
        assert len(rows) == count + 1
        matrix = [[float(x) for x in row.split("\t")[1:]] for row in rows[1:]]
        draws = (reports / f"native{suffix}_draws.tsv").read_text().splitlines()
        draw_matrix = [[float(x) for x in row.split("\t")[1:]] for row in draws[1:]]
        assert len(draw_matrix) == count
        for i in range(count):
            assert matrix[i][i] == .5
            for j in range(count):
                assert abs(matrix[i][j] + matrix[j][i] - 1) < 1e-6
                assert 0 <= draw_matrix[i][j] <= 1
                assert draw_matrix[i][j] == draw_matrix[j][i]
        assert (reports / f"native{suffix}_cycles.tsv").read_text().startswith("a\tb\tc\t")
    assert "jsd" in (reports / "native_behavior.tsv").read_text().lower()
