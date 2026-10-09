"""Independent NumPy oracle for test_wld_network.cu's BF16 tensor dumps."""
import argparse
from pathlib import Path

import numpy as np


def bf16(values):
    bits = np.asarray(values, dtype=np.float32).copy().view(np.uint32)
    bits += np.uint32(0x7fff) + ((bits >> 16) & 1)
    return (bits & np.uint32(0xffff0000)).view(np.float32)


def check(directory):
    def read(mode, graph, name, shape):
        values = np.fromfile(directory / f"m{mode}_g{graph}" / f"{name}.f32", np.float32)
        assert np.isfinite(values).all(), name
        return values.reshape(shape)

    names = ["decoded", "grad_hidden", "critic_input", "critic_middle", "critic_raw",
        "critic_grad_raw", "critic_w1", "critic_w2", "critic_dw1", "critic_dw2"]
    actor = read(0, 0, "decoded", (5, 1979))[:, :-1]
    gradient = np.array([1, -.5, .25, 1, -.75], np.float32)
    errors = {}
    for mode in range(3):
        for name in names:
            np.testing.assert_array_equal(read(mode, 0, name, (-1,)),
                read(mode, 1, name, (-1,)), err_msg=f"CUDA graph parity: {mode}/{name}")
        decoded = read(mode, 0, "decoded", (5, 1979))
        np.testing.assert_array_equal(actor, decoded[:, :-1])
        rows = 5 if mode == 0 else 10
        x = read(mode, 0, "critic_input", (rows, 264))
        expected_input = np.zeros_like(x)
        expected_input[:, 256] = 1
        if mode == 0:
            expected_input[:, :256] = bf16(((np.arange(5 * 256) * 13) % 37 - 18)
                .reshape(5, 256) / 32)
        else:
            features = bf16(((np.arange(256)[None, :] * 17
                + np.arange(5)[:, None] * 19) % 101 - 50) / 64)
            features[4, :128] = features[0, 128:]
            features[4, 128:] = features[0, :128]
            features[3, 128:] = features[3, :128]
            expected_input[:, :128] = features.reshape(10, 128)
        np.testing.assert_array_equal(x, expected_input,
            err_msg=f"same-state feature packing: {mode}")
        w1 = read(mode, 0, "critic_w1", (128, 264))
        w2 = read(mode, 0, "critic_w2", (1, 136))
        middle = bf16(x @ w1.T)
        augmented = np.zeros((rows, 136), np.float32)
        augmented[:, :128] = np.maximum(middle, 0)
        augmented[:, 128] = 1
        raw = bf16(augmented @ w2.T)
        actual_raw = read(mode, 0, "critic_raw", (rows, 1))
        expected_value = (bf16(np.tanh((raw[::2] - raw[1::2]) * np.float32(.5)))
            if mode == 2 else raw[::2] if mode == 1 else raw)
        actual_value = decoded[:, -1:]
        if mode == 0:
            grad_raw = bf16(gradient[:, None])
        else:
            if mode == 2:
                # Backward consumes the stored, precision-rounded forward value.
                own = gradient * np.float32(.5) * (1 - actual_value[:, 0] ** 2)
            else:
                own = gradient
            grad_raw = np.zeros((10, 1), np.float32)
            grad_raw[::2, 0] = bf16(own)
            if mode == 2:
                grad_raw[1::2, 0] = bf16(-own)
            np.testing.assert_array_equal(read(mode, 0, "grad_hidden", (5, 256)), 0)
        dw2 = bf16(grad_raw.T @ augmented)
        grad_middle_aug = bf16(grad_raw @ w2)
        grad_middle = grad_middle_aug[:, :128] * (augmented[:, :128] > 0)
        dw1 = bf16(grad_middle.T @ x)
        for name, expected, actual in [
            ("middle", augmented, read(mode, 0, "critic_middle", (rows, 136))),
            ("raw", raw, actual_raw), ("value", expected_value, actual_value),
            ("grad_raw", grad_raw, read(mode, 0, "critic_grad_raw", (rows, 1))),
            ("dw1", dw1, read(mode, 0, "critic_dw1", (128, 264))),
            ("dw2", dw2, read(mode, 0, "critic_dw2", (1, 136))),
        ]:
            errors[f"{mode}/{name}"] = float(np.max(np.abs(expected - actual)))
            # Allow BF16 accumulation-order differences near rounding boundaries.
            np.testing.assert_allclose(actual, expected, rtol=.012, atol=.004,
                err_msg=f"independent oracle: {mode}/{name}")
        if mode == 2:
            assert actual_value[0, 0] == -actual_value[4, 0]
            assert actual_value[3, 0] == 0
            assert np.max(np.abs(actual_value)) <= 1
            assert np.count_nonzero(dw1) and np.count_nonzero(dw2)
    print("PASS: independent BF16 forward/backward oracle, unchanged actor logits,")
    print("CUDA graph parity, bounded antisymmetry, equal-view draw, critic-only gradients")
    print("maximum absolute errors:", errors)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    check(parser.parse_args().directory)
