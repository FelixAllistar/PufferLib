import sys
import ctypes
import subprocess
import tempfile
import unittest
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from export_onnx import NativeMinGRU, load_weights, read_env_schema


class ExportTests(unittest.TestCase):
    def test_native_recurrent_math(self):
        root = Path(__file__).resolve().parents[2]
        rng = np.random.default_rng(17)
        model = NativeMinGRU(7, 8, 2, 5)
        count = sum(parameter.numel() for parameter in model.parameters())
        with tempfile.TemporaryDirectory() as directory:
            checkpoint = Path(directory) / "weights.bin"
            rng.normal(0, 0.1, count).astype(np.float32).tofile(checkpoint)
            load_weights(checkpoint, model)
            library = Path(directory) / "reference.so"
            subprocess.run(["cc", "-O2", "-shared", "-fPIC", f"-I{root / 'src'}",
                str(Path(__file__).with_name("onnx_native_reference.c")),
                "-lm", "-o", str(library)], check=True)
            native = ctypes.CDLL(str(library)).reference
            array = np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS")
            native.argtypes = [ctypes.c_char_p, *([ctypes.c_int] * 5), *([array] * 4)]
            native.restype = None
            state = rng.normal(size=(2, 3, 8)).astype(np.float32)
            for step in range(32):
                if step % 7 == 0:
                    state[:, 1] = 0
                observations = rng.normal(size=(3, 7)).astype(np.float32)
                fused = np.zeros((3, 6), dtype=np.float32)
                next_state = np.zeros_like(state)
                native(str(checkpoint).encode(), 3, 7, 8, 2, 5,
                    observations, state, fused, next_state)
                with torch.no_grad():
                    logits, value, carry = model(torch.from_numpy(observations),
                        torch.from_numpy(state))
                np.testing.assert_allclose(fused[:, :5], logits.numpy(), atol=1e-6)
                np.testing.assert_allclose(fused[:, 5:], value.numpy(), atol=1e-6)
                np.testing.assert_allclose(next_state, carry.numpy(), atol=1e-6)
                state = next_state

    def test_custom_architecture_rejected(self):
        with self.assertRaises(ValueError):
            read_env_schema("kaggriculture")

    def test_environment_schemas(self):
        self.assertEqual(read_env_schema("abyss"), (1224, [66, 129, 65, 2, 2, 130]))
        self.assertEqual(read_env_schema("puffer_survivors"), (337, [10, 3]))
        self.assertEqual(read_env_schema("bomberman"), (1200, [6]))

    def test_flat_checkpoint_order(self):
        model = NativeMinGRU(7, 8, 2, 5)
        count = sum(parameter.numel() for parameter in model.parameters())
        values = np.arange(count, dtype=np.float32)
        with tempfile.TemporaryDirectory() as directory:
            checkpoint = Path(directory) / "weights.bin"
            values.tofile(checkpoint)
            loaded = load_weights(checkpoint, model)
        self.assertEqual(list(loaded), [
            "encoder.weight", "decoder.weight",
            "layers.0.weight", "layers.1.weight",
        ])
        flattened = np.concatenate([
            model.encoder.weight.detach().numpy().ravel(),
            model.decoder.weight.detach().numpy().ravel(),
            *(layer.weight.detach().numpy().ravel() for layer in model.layers),
        ])
        np.testing.assert_array_equal(flattened, values)

    def test_incomplete_checkpoint_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            checkpoint = Path(directory) / "weights.bin"
            checkpoint.write_bytes(b"abc")
            with self.assertRaises(ValueError):
                load_weights(checkpoint, NativeMinGRU(7, 8, 2, 5))


if __name__ == "__main__":
    unittest.main()
