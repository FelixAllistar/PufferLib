import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "payoff_matrix", Path(__file__).resolve().parents[1] / "payoff_matrix.py")
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)


class PayoffTests(unittest.TestCase):
    def test_native_command_and_actual_games(self):
        output = "draw_rate 0.250\nCUDA_EVAL env=bomberman score=0.625 perf=0.625 games=32\n"
        with patch.object(matrix.subprocess, "run", return_value=
                subprocess.CompletedProcess([], 0, output)) as run:
            self.assertEqual(matrix.match("bomberman", Path("a.bin"), Path("b.bin"),
                16, ["base.cudagraphs=-1"], Path("native")), (0.625, 0.25, 32))
            command = run.call_args.args[0]
            self.assertEqual(command[1:3], ["match", "--headless"])
            self.assertIn("--base.eval_episodes=16", command)
            self.assertIn("--base.cudagraphs=-1", command)
            self.assertNotIn("bomberman", command)

    def test_wrong_environment_rejected(self):
        output = "draw_rate 0\nCUDA_EVAL env=wrong score=0.5 perf=0.5 games=32\n"
        with patch.object(matrix.subprocess, "run", return_value=
                subprocess.CompletedProcess([], 0, output)):
            with self.assertRaises(ValueError):
                matrix.match("bomberman", Path("a"), Path("b"), 16, [], Path("native"))

    def test_one_checkpoint_and_cycles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ["1.bin", "2.bin", "3.bin"]:
                (root / name).touch()
            self.assertEqual(matrix.checkpoints(root, 1), [root / "3.bin"])
        scores = [[0.5, 0.8, 0.2], [0.2, 0.5, 0.8], [0.8, 0.2, 0.5]]
        self.assertEqual(len(matrix.cycles(scores, ["a", "b", "c"], 0.01)), 1)


if __name__ == "__main__":
    unittest.main()
