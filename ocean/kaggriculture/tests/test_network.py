"""Independent NumPy forward/backward reference for the entity network.

Opt-in with KAGGRICULTURE_GPU_TEST_BINARY. No local GPU is touched by default.
"""

import os
from pathlib import Path
import subprocess

import pytest


@pytest.mark.parametrize("hidden", [32, 64])
@pytest.mark.parametrize("graphs", [0, 1])
@pytest.mark.parametrize("loss", [0, 1, 2])
def test_entity_network_reference(tmp_path, hidden, graphs, loss):
    binary = os.environ.get("KAGGRICULTURE_GPU_TEST_BINARY")
    if binary is None:
        pytest.skip("set KAGGRICULTURE_GPU_TEST_BINARY on an idle GPU")
    import numpy as np

    result = subprocess.run([str(Path(binary).resolve()), "network", str(hidden),
        str(graphs), str(loss), str(tmp_path)], text=True, capture_output=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    bf16 = "precision_bytes=2" in result.stdout

    def read(name):
        return np.fromfile(tmp_path / f"{name}.f32", dtype=np.float32)

    def quantize(x):
        x = np.asarray(x, dtype=np.float32)
        if not bf16:
            return x
        bits = x.view(np.uint32)
        return ((bits + 0x7fff + ((bits >> 16) & 1)) & 0xffff0000).view(np.float32)

    def augment(x):
        out = np.zeros((len(x), (x.shape[1] + 8) & ~7), dtype=np.float32)
        out[:, :x.shape[1]] = x
        out[:, x.shape[1]] = 1
        return out

    flat = read("weights")
    offset = 0
    layers = []
    specs = [(184, 64, 64, True), (56, 32, 32, True), (24, 32, 32, True),
        (32, 16, 16, True), (880, hidden, hidden, True),
        (hidden, hidden // 2, 748, False), (hidden, hidden // 2, 1230, False),
        (hidden, hidden // 2, 1, False)]
    for inputs, middle, outputs, relu in specs:
        weights = []
        for rows, features in [(middle, inputs), (outputs, middle)]:
            width = (features + 8) & ~7
            count = rows * width
            w = flat[offset:offset + count].reshape(rows, width)
            assert np.all(w[:, features:] == 0)  # zero bias and dead padding at init
            weights.append(w)
            offset += count
        layers.append(dict(w1=weights[0], w2=weights[1], relu=relu, inputs=inputs))
    assert offset == len(flat)

    def forward(index, x):
        layer = layers[index]
        x = augment(x)
        middle = augment(np.maximum(quantize(x @ layer["w1"].T), 0))
        output = quantize(middle @ layer["w2"].T)
        if layer["relu"]:
            output = np.maximum(output, 0)
        layer.update(x=x, middle=middle, output=output)
        return output

    def backward(index, gradient):
        layer = layers[index]
        if layer["relu"]:
            gradient = np.where(layer["output"] > 0, gradient, 0)
        middle = layer["middle"]
        gm = quantize(gradient @ layer["w2"])[:, :middle.shape[1] - 8]
        # All hidden widths in this fixture are multiples of eight.
        gm = np.where(middle[:, :gm.shape[1]] > 0, gm, 0)
        layer["dw2"] = quantize(gradient.T @ middle)
        layer["dw1"] = quantize(gm.T @ layer["x"])
        return quantize(gm @ layer["w1"])[:, :layer["inputs"]]

    obs = read("observations").reshape(5, 1424)
    entities = [np.concatenate([obs[:, :128], obs[:, 1368:]], axis=1),
        obs[:, 128:632].reshape(45, 56), obs[:, 632:824].reshape(40, 24),
        obs[:, 824:1368].reshape(85, 32)]
    fused = np.concatenate([forward(i, x).reshape(5, -1)
        for i, x in enumerate(entities)], axis=1)
    encoded = forward(4, fused)
    decoded = np.concatenate([forward(i, encoded) for i in range(5, 8)], axis=1)
    gl = ((np.arange(5 * 1978) * 17) % 31 - 15).reshape(5, 1978) / 128
    gv = (np.arange(5) - 2).reshape(5, 1) / 16
    if loss == 1:
        gv[:] = 0
    if loss == 2:
        gl[:] = 0
    upstream = quantize(sum(backward(i, quantize(g)) for i, g in
        zip(range(5, 8), [gl[:, :748], gl[:, 748:], gv])))
    gf = backward(4, upstream)
    for i, gradient in enumerate(np.split(gf, [64, 352, 608], axis=1)):
        backward(i, gradient.reshape(layers[i]["output"].shape))
    gradients = np.concatenate([layer[key].ravel() for layer in layers
        for key in ["dw1", "dw2"]])
    # GPU and NumPy BLAS reductions can differ at BF16 rounding boundaries.
    for name, expected in [("encoded", encoded), ("decoded", decoded),
            ("upstream", upstream), ("gradients", gradients)]:
        actual = read(name).reshape(expected.shape)
        assert np.isfinite(actual).all()
        scale = np.max(np.abs(expected))
        assert scale > 0
        np.testing.assert_allclose(actual, expected, rtol=0.03 if bf16 else 2e-4,
            atol=scale * (0.008 if bf16 else 3e-6), err_msg=name)
    for i in ([5, 6] if loss == 2 else [7] if loss == 1 else []):
        assert not np.any(layers[i]["dw1"])
        assert not np.any(layers[i]["dw2"])


@pytest.mark.parametrize("graphs", [0, 1])
def test_legacy_checkpoint_recurrent_outputs(tmp_path, graphs):
    binary = os.environ.get("KAGGRICULTURE_GPU_TEST_BINARY")
    checkpoint = os.environ.get("KAGGRICULTURE_COMPAT_CHECKPOINT")
    dataset = os.environ.get("KAGGRICULTURE_COMPAT_DATA")
    if not all([binary, checkpoint, dataset]):
        pytest.skip("set the GPU binary and compatibility checkpoint/dataset paths")
    import numpy as np

    result = subprocess.run([str(Path(binary).resolve()), "checkpoint", checkpoint,
        str(graphs), dataset, str(tmp_path)], text=True, capture_output=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    bf16 = "precision_bytes=2" in result.stdout

    def read(name):
        return np.fromfile(tmp_path / f"{name}.f32", dtype=np.float32)

    def q(x):
        x = np.asarray(x, dtype=np.float32)
        if not bf16:
            return x
        bits = x.view(np.uint32)
        return ((bits + 0x7fff + ((bits >> 16) & 1)) & 0xffff0000).view(np.float32)

    original = np.fromfile(checkpoint, dtype=np.float32)
    flat = read("weights")
    np.testing.assert_array_equal(flat, q(original))
    pos = 0

    def take(rows, columns):
        nonlocal pos
        matrix = flat[pos:pos + rows * columns].reshape(rows, columns)
        pos += rows * columns
        return matrix

    specs = [(184, 64, 64), (56, 32, 32), (24, 32, 32), (32, 16, 16),
        (880, 256, 256), (256, 128, 748), (256, 128, 1230), (256, 128, 1)]
    mlps = [(take(mid, (ni + 8) & ~7), take(no, (mid + 8) & ~7))
        for ni, mid, no in specs]
    gru = [take(768, 256) for _ in range(2)]
    assert pos == len(flat) == 1082200

    def aug(x):
        out = np.zeros((*x.shape[:-1], (x.shape[-1] + 8) & ~7), np.float32)
        out[..., :x.shape[-1]] = x
        out[..., x.shape[-1]] = 1
        return out

    def mlp(i, x):
        a, b = mlps[i]
        y = q(aug(np.maximum(q(aug(x) @ a.T), 0)) @ b.T)
        return np.maximum(y, 0) if i < 5 else y

    def sigmoid(x):
        return 1 / (1 + np.exp(-np.clip(x, -80, 80)))

    state = np.zeros((2, 256), np.float32)
    outputs, states = [], []
    for t, obs in enumerate(read("observations").reshape(32, 1424)):
        if t % 16 == 0:
            state.fill(0)
        x = mlp(4, np.concatenate([
            mlp(0, np.concatenate([obs[:128], obs[1368:]])),
            mlp(1, obs[128:632].reshape(9, 56)).ravel(),
            mlp(2, obs[632:824].reshape(8, 24)).ravel(),
            mlp(3, obs[824:1368].reshape(17, 32)).ravel()]))
        for layer, weights in enumerate(gru):
            a, g, h = np.split(q(weights @ x), 3)
            candidate = np.where(a >= 0, a + 0.5, sigmoid(a))
            z = sigmoid(g)
            updated = state[layer] + z * (candidate - state[layer])
            x = q(sigmoid(h) * updated + (1 - sigmoid(h)) * x)
            state[layer] = q(updated)
        outputs.append(np.concatenate([mlp(i, x) for i in (5, 6, 7)]))
        states.append(state.copy())
    for name, expected in [("decoded", np.array(outputs)), ("states", np.array(states))]:
        actual = read(name).reshape(expected.shape)
        assert np.isfinite(actual).all()
        np.testing.assert_allclose(actual, expected, rtol=0.03 if bf16 else 5e-5,
            atol=np.max(np.abs(expected)) * (0.006 if bf16 else 2e-6), err_msg=name)
