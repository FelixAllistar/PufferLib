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

    def test_game_score_uses_physical_progress_and_clears(self):
        report = dict(passed=True, mismatches=0, objective="full_game",
                      scope="natural_all_32_stages", reference_frames=0, eligible_starts=32,
                      episodes_requested=32, episodes_run=32, episodes_completed=32,
                      seed=137, max_frames=6000, deterministic=0,
                      all_episodes_run_to_terminal=True, ram_writes_after_reset=0,
                      game_clear_episodes=8, rom_successes=0, native_successes=0,
                      mean_progress_fraction=0.5)
        parts = sweep.game_panel_score(report, 32, 137, 6000)
        self.assertEqual(parts, dict(game_score=0.375, game_clear_rate=0.25,
                                     game_progress_fraction=0.5))
        report.update(episode_return=1000, checkpoint_reward=100, curriculum_frames=425)
        self.assertEqual(sweep.game_panel_score(report, 32, 137, 6000), parts)
        for key, value in (("episodes_completed", 31), ("game_clear_episodes", 33),
                           ("mean_progress_fraction", float("nan")),
                           ("mean_progress_fraction", 1.1), ("mismatches", 1),
                           ("max_frames", 1800), ("scope", "natural_1_1_new_game"),
                           ("ram_writes_after_reset", 1), ("native_successes", 1)):
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    sweep.game_panel_score(report | {key: value}, 32, 137, 6000)

    def test_all_rollout_shapes_train_and_request_the_same_budget(self):
        config = sweep.read_config(Path(__file__).parent / "profiles/sweep.ini")
        self.assertEqual(config["base"]["load_model_path"], "None")
        self.assertEqual(config.getint("fpg", "curriculum_resume"), 0)
        self.assertEqual(config["env"]["mode"], "mixed")
        self.assertEqual(config.getint("train", "anneal_lr"), 0)
        self.assertEqual(config.getint("train", "anneal_ent_coef"), 0)
        self.assertEqual(config.getint("vec", "total_agents"), 1024)
        self.assertEqual(config.getint("vec", "num_buffers"), 2)
        self.assertEqual(len(sweep.validate_search(config)), 17)
        values = sweep.flattened(config)
        for name in config.sections():
            if name.startswith("sweep."):
                self.assertLessEqual(float(config[name]["min"]), float(values[name[6:]]))
                self.assertLessEqual(float(values[name[6:]]), float(config[name]["max"]))
        steps = config.getint("train", "total_timesteps")
        for key in ("min", "max"):
            self.assertEqual(int(config["sweep.train.total_timesteps"][key]), steps)

        def choices(key):
            return sweep.integer_choices(config, key)

        for agents, horizon, minibatch in itertools.product(
                choices("vec.total_agents"), choices("train.horizon"), choices("train.minibatch_size")):
            with self.subTest(agents=agents, horizon=horizon, minibatch=minibatch):
                self.assertEqual(horizon % 8, 0)
                self.assertEqual(minibatch % horizon, 0)
                self.assertLessEqual(minibatch, agents * horizon)
                self.assertEqual(agents % (minibatch // horizon), 0)
                actual = sweep.completed_steps(steps, agents, horizon)
                self.assertEqual(actual % (agents * horizon), 0)
                self.assertLessEqual(actual, steps)
                self.assertLess(steps - actual, agents * horizon)
                self.assertGreaterEqual(int(float(config["sweep.train.replay_ratio"]["min"])
                                            * agents * horizon / minibatch), 1)
        for hidden in choices("policy.hidden_size"):
            self.assertEqual(hidden % 32, 0)

    def test_invalid_or_nonfresh_profiles_are_rejected(self):
        config = sweep.read_config(Path(__file__).parent / "profiles/sweep.ini")
        for section, key, value in (("base", "load_model_path", "latest"),
                                    ("env", "mode", "fpg"),
                                    ("train", "anneal_lr", "1"),
                                    ("train", "anneal_ent_coef", "1"),
                                    ("sweep.vec.total_agents", "max", "2048"),
                                    ("sweep.train.minibatch_size", "max", "65536"),
                                    ("sweep", "eval_game_episodes", "16")):
            with self.subTest(section=section, key=key):
                bad = copy.deepcopy(config)
                bad[section][key] = value
                with self.assertRaises(ValueError):
                    sweep.validate_search(bad)
        self.assertEqual(sweep.completed_steps(30_000_000, 1024, 128), 29_884_416)
        with self.assertRaises(ValueError):
            sweep.completed_steps(100, 1024, 128)

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
