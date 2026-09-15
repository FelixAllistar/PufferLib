import os
from pathlib import Path
import shlex
import subprocess
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "dense_experiment.sh"


class LauncherTests(unittest.TestCase):
    def command(self, *args):
        result = subprocess.run(["bash", str(SCRIPT), *args],
                                env={**os.environ, "KAG_DRY_RUN": "1"},
                                check=True, text=True, capture_output=True)
        return shlex.split(result.stdout)

    def test_config_is_not_overridden(self):
        for mode, executor in [(0, 0), (1, 0), (1, 1), (2, 0), (2, 1), (3, 0)]:
            args = self.command(str(mode), str(executor))
            self.assertFalse(any(x.startswith(("env.reward_", "env.pbrs_", "train.",
                                              "base.load_model_path=", "env.reset_state_prob=",
                                              "env.macro_score_features=")) for x in args))
            self.assertIn("env.frozen_macro_mode=-1", args)
            self.assertIn("env.frozen_macro_executor_version=-1", args)

    def test_explicit_preset_and_final_override(self):
        args = self.command("--dense-preset", "2", "1", "env.reward_quality_scale=4",
                            "base.load_model_path=chosen.bin")
        effective = dict(x.split("=", 1) for x in args if "=" in x)
        self.assertEqual(effective["env.reward_money_timing"], "1")
        self.assertEqual(effective["env.reward_quality_scale"], "4")
        self.assertEqual(effective["base.load_model_path"], "chosen.bin")

    def test_invalid_combo(self):
        result = subprocess.run(["bash", str(SCRIPT), "3", "1"], capture_output=True)
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
