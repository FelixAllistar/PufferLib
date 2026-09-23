"""Opt-in standalone BC GPU tests. Training data and outputs are test-owned."""
import os
from pathlib import Path
import re
import struct
import subprocess

import numpy as np
import pytest


ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture(scope="module")
def binary():
    name = os.environ.get("KAGGRICULTURE_BC_TEST_BINARY")
    if not name:
        pytest.skip("set KAGGRICULTURE_BC_TEST_BINARY to opt into GPU BC tests")
    return Path(name).resolve()


@pytest.fixture(scope="module")
def dataset(tmp_path_factory):
    directory = tmp_path_factory.mktemp("offline_bc")
    path = directory / "test.bc"
    games, steps, obs, heads, packed = 3, 720, 1424, 47, 248
    count = games * steps
    rng = np.random.default_rng(73)
    observations = rng.uniform(0, 1, (count, obs)).astype("<f4")
    labels = np.zeros((count, heads), dtype="<f4")
    labels[steps - 1::steps] = -1
    masks = np.full((count, packed), 255, dtype="u1")
    targets = np.tile(np.linspace(5, 1, steps, dtype="<f4"), games)
    targets[steps - 1::steps] = np.nan
    with path.open("wb") as file:
        file.write(struct.pack("<16I2Qd", 0x4b414742, 3, count, obs, heads, packed, games,
            steps, 4, 3, 5, 2, 2, 1, 0, 1, 123, 456, 0.999817491))
        for values in (observations, labels, masks, targets):
            file.write(values.tobytes())
    return path


def train(binary, dataset, output, mode="actor", initial="None", epochs=2, rate=0.0001):
    options = {"bc.data": dataset, "bc.output": output, "bc.mode": mode,
        "bc.batch": 2, "bc.epochs": epochs, "bc.max_batches": 1, "bc.value_coef": 1,
        "bc.learning_rate": rate, "base.load_model_path": initial, "base.seed": 73,
        "policy.hidden_size": 32, "policy.num_layers": 1, "train.gamma": 0.999817491}
    result = subprocess.run([str(binary)] + [f"--{key}={value}" for key, value in options.items()],
        cwd=ROOT, text=True, capture_output=True, timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "2 train / 1 held-out" in result.stdout
    assert "split=holdout" in result.stdout
    assert "nan" not in result.stdout.lower(), result.stdout
    return result.stdout


@pytest.fixture(scope="module")
def source(binary, dataset):
    path = dataset.parent / "source.bin"
    train(binary, dataset, path, epochs=0)
    return path


@pytest.mark.parametrize("mode", ["actor", "critic", "joint"])
@pytest.mark.parametrize("rate", [0, 0.0001])
def test_offline_modes_freeze_correct_parameters(binary, dataset, source, tmp_path, mode, rate):
    output = tmp_path / "trained.bin"
    log = train(binary, dataset, output, mode, source, rate=rate)
    before, after = [np.fromfile(path, "<f4") for path in (source, output)]
    assert np.isfinite(after).all()
    assert before.shape == after.shape
    begin, end = map(int, re.search(r"critic_range=(\d+):(\d+)", log).groups())
    critic = np.zeros(len(before), dtype=bool)
    critic[begin:end] = True
    changed = before != after
    if rate == 0:
        assert not changed.any()
    elif mode == "critic":
        assert changed[critic].any() and not changed[~critic].any()
    elif mode == "actor":
        assert changed[~critic].any() and not changed[critic].any()
    else:
        assert changed[critic].any() and changed[~critic].any()


def test_refuses_overwrite(binary, dataset, source):
    original = source.read_bytes()
    with pytest.raises(AssertionError):
        train(binary, dataset, source)
    assert source.read_bytes() == original
