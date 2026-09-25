"""Submission inference versus the native FP32 encoder, MinGRU and decoder."""
import os
from pathlib import Path
import struct
import subprocess

import numpy as np
import pytest

from entity_agent import EntityModel

ROOT = Path(__file__).resolve().parents[3]


@pytest.mark.parametrize("graphs", [0, 1])
@pytest.mark.parametrize("seed", [7, 42])
def test_submission_native_recurrence(tmp_path, graphs, seed):
    binary = os.environ.get("KAGGRICULTURE_GPU_TEST_BINARY")
    if not binary:
        pytest.skip("set KAGGRICULTURE_GPU_TEST_BINARY to opt into GPU inference")
    rng = np.random.default_rng(seed)
    hidden, layers = 256, 2
    checkpoint = tmp_path / "model.bin"
    supplied = os.environ.get("KAG_EXPORT_CHECKPOINT")
    if supplied:
        checkpoint = Path(supplied).resolve()
    else:
        weights = []
        for inputs, middle, outputs in [(184, 64, 64), (56, 32, 32), (24, 32, 32),
            (32, 16, 16), (880, hidden, hidden), (hidden, hidden // 2, 748),
            (hidden, hidden // 2, 1230), (hidden, hidden // 2, 1)]:
            for rows, features in [(middle, inputs), (outputs, middle)]:
                matrix = np.zeros((rows, (features + 8) & ~7), dtype="<f4")
                matrix[:, :features + 1] = rng.normal(0, 0.04, (rows, features + 1))
                weights.append(matrix.ravel())
        weights.append(rng.normal(0, 0.04, layers * 3 * hidden * hidden).astype("<f4"))
        np.concatenate(weights).tofile(checkpoint)
    observations = rng.uniform(-0.2, 2, (32, 1424)).astype("<f4")
    data = tmp_path / "observations.bc"
    # The existing native checkpoint oracle reads a v3 header plus 32 rows.
    # This is deliberately an observation excerpt, not a training dataset.
    with data.open("wb") as stream:
        stream.write(struct.pack("<16IQQd", 0x4b414742, 3, 32, 1424, 47, 248,
            1, 32, 4, 3, 5, 2, 2, 1, 0, 0, 123, 456, 0.99))
        stream.write(observations.tobytes())
    result = subprocess.run([str(Path(binary).resolve()), "checkpoint", str(checkpoint),
        str(graphs), str(data), str(tmp_path)], cwd=ROOT, capture_output=True,
        text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "precision_bytes=4" in result.stdout, "this test qualifies FP32 only"
    model = EntityModel(checkpoint, hidden, layers)
    expected = np.fromfile(tmp_path / "decoded.f32", "<f4").reshape(32, 1979)
    states = np.fromfile(tmp_path / "states.f32", "<f4").reshape(32, layers, hidden)
    actual, carries = [], []
    for step, observation in enumerate(observations):
        if step % 16 == 0:
            model.reset()
        actual.append(model.forward(observation))
        carries.append(model.state.copy())
    np.testing.assert_allclose(actual, expected, atol=2e-4, rtol=3e-4)
    np.testing.assert_allclose(carries, states, atol=2e-4, rtol=3e-4)
    print(f"export max_logit_error={np.max(np.abs(actual - expected)):.9g} "
        f"max_state_error={np.max(np.abs(carries - states)):.9g}")
