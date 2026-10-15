"""CPU checks for native sweep inputs and progress-result reporting."""
import copy
import configparser
import itertools
from pathlib import Path
import tempfile
import unittest

import sweep


class SweepChecks(unittest.TestCase):
    def config(self):
        return sweep.read_config(Path(__file__).parent / "profiles/sweep.ini")

    def test_native_sweep_has_no_evaluator_or_worker_dependency(self):
        config = self.config()
        self.assertEqual(config["sweep"]["worker_path"], "None")
        self.assertEqual(config["sweep"]["metric"], "sweep_score")
        self.assertNotIn("evaluator_path", config["sweep"])
        self.assertEqual(config.getint("base", "eval_episodes"), 0)
        self.assertEqual(config.getint("sweep", "max_failures"), 1000)
        self.assertEqual(config.getint("train", "total_timesteps"), 2_000_000_000)
        self.assertEqual(len(sweep.validate_search(config)), 15)

    def test_all_rollout_shapes_train_at_the_requested_budget(self):
        config = self.config()
        sweep.validate_search(config)
        requested = config.getint("train", "total_timesteps")
        for n, h, m in itertools.product(sweep.integer_choices(config, "vec.total_agents"),
                sweep.integer_choices(config, "train.horizon"),
                sweep.integer_choices(config, "train.minibatch_size")):
            with self.subTest(agents=n, horizon=h, minibatch=m):
                actual = sweep.completed_steps(requested, n, h)
                self.assertEqual(actual % (n * h), 0)
                self.assertLessEqual(actual, requested)
                self.assertLess(requested - actual, n * h)
                self.assertGreaterEqual(int(float(config["sweep.train.replay_ratio"]["min"])
                                           * n * h / m), 1)

    def test_large_ranges_and_cosine_are_allowed(self):
        config = self.config()
        config["sweep.vec.total_agents"]["max"] = "8192"
        config["sweep.train.horizon"]["max"] = "1024"
        config["sweep.train.minibatch_size"]["min"] = "1024"
        config["sweep.train.minibatch_size"]["max"] = "8192"
        config["train"]["anneal_lr"] = "1"
        config["train"]["anneal_ent_coef"] = "1"
        self.assertIn("vec.total_agents", sweep.validate_search(config))

    def test_invalid_or_nonfresh_profiles_are_rejected(self):
        config = self.config()
        for section, key, value in (("base", "load_model_path", "latest"),
                                   ("base", "eval_episodes", "16"),
                                   ("env", "mode", "fpg"),
                                   ("fpg", "curriculum_resume", "1"),
                                   ("sweep", "worker_path", "missing_evaluator.py"),
                                   ("sweep.train.total_timesteps", "min", "30000000"),
                                   ("sweep.train.minibatch_size", "max", "131072"),
                                   ("vec", "num_buffers", "3")):
            with self.subTest(section=section, key=key):
                bad = copy.deepcopy(config)
                bad[section][key] = value
                with self.assertRaises(ValueError):
                    sweep.validate_search(bad)
        with self.assertRaises(ValueError):
            sweep.completed_steps(100, 1024, 128)

    def test_summary_reads_native_metric_curves_and_skips_incomplete_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            logs = root / "logs/mario_sim"
            logs.mkdir(parents=True)
            (logs / "sweep_1_0.ini").write_text("[metrics]\nenv/sweep_score=0.1,0.4\n"
                "env/game_progress_pixels=100,1200\nenv/curriculum_frames=3,96\n"
                "uptime=10,90\nagent_steps=1000000,89980928\n")
            (logs / "sweep_1_1.ini").write_text("[train]\ntotal_timesteps=90000000\n")
            rows = sweep.results(root)
            self.assertEqual(len(rows), 1)
            self.assertEqual((rows[0]["score"], rows[0]["game_pixels"],
                              rows[0]["curriculum_frames"], rows[0]["steps"]),
                             (0.4, 1200, 96, 89980928))
            self.assertEqual(sweep.results(logs), rows)

    def test_every_overlay_argument_is_declared_for_native_cli(self):
        native = configparser.ConfigParser(interpolation=None)
        native.read([sweep.ROOT / "config/default.ini", sweep.ROOT / f"config/{sweep.ENV}.ini"])
        overlay = self.config()
        for section in overlay.sections():
            self.assertIn(section, native)
            for key in overlay[section]:
                self.assertIn(key, native[section], f"native CLI rejects {section}.{key}")


if __name__ == "__main__":
    unittest.main()
