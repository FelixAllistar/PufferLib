import os
import ctypes
import hashlib
import importlib
import importlib.metadata
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_suffix(".c")
HEADER = ROOT / "ocean/kaggriculture/core.h"


def test_official_market_prices(tmp_path):
    if os.environ.get("KAGGRICULTURE_OFFICIAL_PARITY") != "1":
        pytest.skip("set KAGGRICULTURE_OFFICIAL_PARITY=1 with kaggle-environments installed")
    official = importlib.import_module("kaggle_environments.envs.kaggriculture.kaggriculture")
    library = tmp_path / "core.so"
    subprocess.run(["cc", "-x", "c", "-O2", "-shared", "-fPIC", str(HEADER),
                    "-lm", "-o", str(library)], check=True, capture_output=True)
    native = ctypes.CDLL(str(library))
    native.kg_market_price.argtypes = [ctypes.c_int, ctypes.c_int]
    native.kg_market_price.restype = ctypes.c_int
    digest = hashlib.sha256(Path(official.__file__).read_bytes()).hexdigest()
    print(f"official environment source SHA256={digest}")
    for product, item in enumerate(official.PRODUCTS):
        for inventory in range(20001):
            expected = official.market_price(item, inventory)
            actual = native.kg_market_price(product, inventory)
            assert actual == expected, (item, inventory, expected, actual, digest)


@pytest.mark.parametrize("seed", [7, 42])
def test_official_full_game(tmp_path, seed):
    if os.environ.get("KAGGRICULTURE_OFFICIAL_PARITY") != "1":
        pytest.skip("set KAGGRICULTURE_OFFICIAL_PARITY=1 with kaggle-environments installed")
    from kaggle_environments import make
    from ocean.kaggriculture import replay_native as bridge
    official = importlib.import_module("kaggle_environments.envs.kaggriculture.kaggriculture")
    library = tmp_path / 'core.so'
    subprocess.run(['cc', '-x', 'c', '-O2', '-shared', '-fPIC', str(HEADER),
                    '-lm', '-o', str(library)], check=True, capture_output=True)
    native = bridge.load_core(library)
    config = bridge.CConfig()
    native.kg_config_default(ctypes.byref(config))
    config.seed = seed
    state = native.kg_create(ctypes.byref(config))
    env = make('kaggriculture', configuration={'seed': seed, 'episodeSteps': 720}, debug=True)
    env.reset()
    try:
        for turn in range(720):
            expected = bridge.canonical_replay_frame(env.state)
            actual = bridge.c_snapshot(native, state)
            assert not bridge.first_difference(expected, actual), (
                seed, turn, bridge.first_difference(expected, actual))
            if turn == 719:
                break
            actions = [official.starter_agent(record.observation) for record in env.state]
            native.kg_step(state, (bridge.CAction * 2)(*(bridge.c_action(a) for a in actions)))
            env.step(actions)
        assert all(record.status == 'DONE' for record in env.state)
        assert native.kg_done(state)
        assert [native.kg_player_money(state, p) for p in range(2)] == [
            record.reward for record in env.state]
        replay = tmp_path / 'official.json'
        replay.write_text(json.dumps(dict(name='kaggriculture',
            module_version=importlib.metadata.version('kaggle-environments'),
            configuration=dict(env.configuration), info={'EpisodeId': seed, 'seed': seed},
            statuses=[record.status for record in env.state],
            rewards=[record.reward for record in env.state], steps=env.steps)))
        index = tmp_path / 'index.tsv'
        index.write_text('source\tepisode_id\tturn\tplayer\tstate_key\n' + ''.join(
            f'{replay}\t{seed}\t{turn}\t0\t{seed}:{turn}:0\n' for turn in (0, 360, 718)))
        bank = tmp_path / 'official.kgb'
        subprocess.run([sys.executable,
            str(ROOT / 'ocean/kaggriculture/build_replay_state_bank.py'), str(replay),
            '--index', str(index), '--output', str(bank), '--lib', str(library)],
            check=True, capture_output=True, text=True, timeout=60)
        report = json.loads(Path(f'{bank}.summary.json').read_text())
        assert report['record_count'] == 3
        assert report['counts']['parity_frames'] == 720
        assert report['counts']['resume_checks'] == 3
    finally:
        native.kg_destroy(state)


@pytest.fixture(params=[False, True], ids=["optimized", "sanitized"])
def build(request, tmp_path):
    compiler = shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("C compiler unavailable")
    flags = ["-O2"]
    if request.param:
        flags = ["-O1", "-g", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer"]

    def compile_header(header, name):
        executable = tmp_path / name
        subprocess.run([
            compiler, "-std=c11", *flags, "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", f'-DCORE_HEADER="{header}"', str(SOURCE),
            "-lm", "-o", str(executable),
        ], check=True, capture_output=True)
        return executable

    return compile_header


def test_rule_regressions(build):
    executable = build(HEADER, "core")
    subprocess.run([executable], check=True, capture_output=True, timeout=30)


def test_python_random_stream(build):
    executable = build(HEADER, "core")
    result = subprocess.run([executable, "--rng"], check=True,
        capture_output=True, text=True, timeout=30)
    generators = {}
    for line in result.stdout.splitlines():
        seed, value = map(int, line.split())
        if seed not in generators:
            generators[seed] = random.Random(seed)
        assert value == generators[seed].getrandbits(32)


@pytest.mark.parametrize("mode", ["--trace", "--rule-trace"])
def test_legacy_state_and_reset_parity(build, mode):
    reference = os.environ.get("KAGGRICULTURE_REFERENCE_ROOT")
    if reference is None:
        pytest.skip("set KAGGRICULTURE_REFERENCE_ROOT to the preserved old checkout")
    old = build(Path(reference).resolve() / "ocean/kaggriculture/kaggriculture_core.c",
        "legacy")
    new = build(HEADER, "ported")
    expected = subprocess.run([old, mode], check=True,
        capture_output=True, timeout=120)
    actual = subprocess.run([new, mode], check=True,
        capture_output=True, timeout=120)
    expected_lines = expected.stdout.splitlines()
    actual_lines = actual.stdout.splitlines()
    assert len(expected_lines) == len(actual_lines) == 24 * 2 * (719 + 29)
    for index, (old_line, new_line) in enumerate(zip(expected_lines, actual_lines)):
        assert old_line == new_line, f"first state mismatch at trace row {index}"
