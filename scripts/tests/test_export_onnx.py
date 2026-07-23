import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from export_onnx import NativeMinGRU, load_weights, read_env_schema


class ExportTests(unittest.TestCase):
    def test_environment_schemas(self):
        self.assertEqual(read_env_schema("abyss"), (328, [5, 3, 3, 3, 3, 3]))
        self.assertEqual(read_env_schema("puffer_survivors"), (396, [9, 3]))

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


if __name__ == "__main__":
    unittest.main()
