import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("prepare_wld", Path(__file__).parents[1] / "prepare_wld.py")
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class WarmStartTests(unittest.TestCase):
    def test_only_critic_changes_and_output_starts_neutral(self):
        from array import array
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root / "source.bin", root / "wld.bin"
            start, count, total = PREPARE.critic_slice()
            original = array("f", [0.125]) * total
            source.write_bytes(original.tobytes())
            result = PREPARE.prepare(source, output)
            converted = array("f", output.read_bytes())
            self.assertEqual(original[:start], converted[:start])
            self.assertEqual(original[start + count:], converted[start + count:])
            self.assertTrue(any(converted[start:start + 128 * 264]))
            self.assertFalse(any(converted[start + 128 * 264:start + count]))
            self.assertTrue(result["actor_and_recurrent_weights_byte_identical"])
            with self.assertRaises(AssertionError):
                PREPARE.prepare(source, output)

    def test_wrong_checkpoint_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root / "bad.bin", root / "wld.bin"
            source.write_bytes(b"bad")
            with self.assertRaises(AssertionError):
                PREPARE.prepare(source, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
