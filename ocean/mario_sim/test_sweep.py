"""CPU checks for comparable scoring and native sweep parameter constraints."""
import copy
import configparser
import itertools
from pathlib import Path
import unittest

import sweep


class SweepChecks(unittest.TestCase):
    def setUp(self):
        self.depths = [64, 96]
        self.reports = [dict(passed=True, mismatches=0, objective="fpg", reference_frames=d,
                             episodes_requested=16, episodes_run=16, episodes_completed=16,
                             seed=137, max_frames=1800, deterministic=0,
                             rom_successes=w, native_successes=w)
                        for d, w in zip(self.depths, (12, 4))]

    def score(self, reports):
        return sweep.panel_score(reports, self.depths, 16, 137, 1800)

    def test_score_counts_actual_successes_on_equal_panel(self):
        self.assertEqual(self.score(self.reports), 0.5)
        for report in self.reports:
            report.update(curriculum_frames=425, episode_return=999, target_frames=1)
        self.assertEqual(self.score(self.reports), 0.5)

    def test_failed_or_different_evaluation_never_enters_search(self):
        for key, value in (("passed", False), ("mismatches", 1), ("objective", "level_transition"),
                           ("reference_frames", 80), ("episodes_completed", 15),
                           ("episodes_run", 17), ("seed", 73), ("max_frames", 3600),
                           ("deterministic", 1), ("native_successes", 11),
                           ("rom_successes", 17)):
            with self.subTest(key=key):
                reports = copy.deepcopy(self.reports)
                reports[0][key] = value
                with self.assertRaises(ValueError):
                    self.score(reports)
        with self.assertRaises(ValueError):
            self.score(self.reports[:1])

    def test_result_pipe_preserves_final_score_budget_and_cost(self):
        packet = sweep.result_packet(0.5, 120, 67108864)
        self.assertEqual(len(packet), 784)
        decoded = sweep.RESULT.unpack(packet)
        self.assertEqual(decoded[:4], (0.5, 120, 67108864, 1))
        self.assertEqual((decoded[4], decoded[68], decoded[132]), (0.5, 120, 67108864))

    def test_all_rollout_shapes_are_legal_and_budgets_are_equal(self):
        config = sweep.read_config(Path(__file__).parent / "profiles/sweep.ini")
        self.assertEqual(config["base"]["load_model_path"], "None")
        self.assertEqual(config.getint("fpg", "curriculum_resume"), 0)
        self.assertEqual(config["env"]["mode"], "fpg")
        values = sweep.flattened(config)
        for name in config.sections():
            if name.startswith("sweep."):
                self.assertLessEqual(float(config[name]["min"]), float(values[name[6:]]))
                self.assertLessEqual(float(values[name[6:]]), float(config[name]["max"]))
        steps = config.getint("train", "total_timesteps")
        for key in ("min", "max"):
            self.assertEqual(int(config["sweep.train.total_timesteps"][key]), steps)

        def choices(key):
            section = config["sweep." + key]
            low, high = (int(section[k]) for k in ("min", "max"))
            return [2**n for n in range(20) if low <= 2**n <= high]

        for agents, horizon, minibatch in itertools.product(
                choices("vec.total_agents"), choices("train.horizon"), choices("train.minibatch_size")):
            with self.subTest(agents=agents, horizon=horizon, minibatch=minibatch):
                self.assertEqual(horizon % 8, 0)
                self.assertEqual(minibatch % horizon, 0)
                self.assertLessEqual(minibatch, agents * horizon)
                self.assertEqual(agents % (minibatch // horizon), 0)
                self.assertEqual(steps % (agents * horizon), 0)
        for hidden in choices("policy.hidden_size"):
            self.assertEqual(hidden % 32, 0)

    def test_every_overlay_argument_is_declared_for_native_cli(self):
        native = configparser.ConfigParser(interpolation=None)
        native.read([sweep.ROOT / "config/default.ini", sweep.ROOT / f"config/{sweep.ENV}.ini"])
        overlay = sweep.read_config(Path(__file__).parent / "profiles/sweep.ini")
        for section in overlay.sections():
            self.assertIn(section, native)
            for key in overlay[section]:
                self.assertIn(key, native[section], f"native CLI rejects {section}.{key}")


if __name__ == "__main__":
    unittest.main()
