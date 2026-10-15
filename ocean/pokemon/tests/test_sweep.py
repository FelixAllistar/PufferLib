"""CPU-only contracts for the fixed-team native/Oak sweep."""
import configparser
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sweep = load("pokemon_sweep", HERE / "sweep.py")
evaluation = load("pokemon_oak_evaluation", HERE / "benchmarks/evaluate_checkpoint.py")


class SweepContracts(unittest.TestCase):
    def test_history_import_validates_completed_trials_before_writing(self):
        source = sweep.ROOT / "reports/pokemon_sweeps/20261015_vast_setup"
        for corrupt in (None, "checkpoint", "budget", "panel"):
            with self.subTest(corrupt=corrupt), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                report = json.loads((source / "default_trial.json").read_text())
                checkpoint = root / "weights.bin"
                checkpoint.write_bytes(b"temporary checkpoint fixture")
                report["checkpoint"] = str(checkpoint)
                report["sha256"] = sweep.sha(checkpoint)
                saved = configparser.ConfigParser(interpolation=None)
                saved.read(source / "default_trial.ini")
                saved["base"]["run_id"] = "sweep_fixture_0000"
                if corrupt == "checkpoint":
                    checkpoint.write_bytes(b"changed checkpoint")
                if corrupt == "budget":
                    saved["metrics"]["agent_steps"] = "1024"
                if corrupt == "panel":
                    report["scores"]["sample"]["games"] = 128
                log = root / "sweep_fixture_0000.ini"
                with log.open("w") as stream:
                    saved.write(stream)
                output = root / "sweep_fixture_0000.ini.oak"
                output.mkdir()
                (output / "summary.json").write_text(json.dumps(report))
                history = root / "history"
                if corrupt:
                    with self.assertRaises(ValueError):
                        sweep.import_history(sweep.read_config(), root, history, root)
                    self.assertFalse(history.exists())
                else:
                    sweep.import_history(sweep.read_config(), root, history, root)
                    self.assertEqual(len(history.read_text().splitlines()), 12)
                    changed = sweep.read_config(overrides=("--base.seed=99",))
                    with self.assertRaises(ValueError):
                        sweep.validate_history_context(changed, history)

    def test_full_learner_budget_and_frozen_architecture(self):
        config = sweep.read_config()
        self.assertEqual(sweep.learner_steps(config), 25_165_824)
        for key in ("policy.hidden_size", "policy.num_layers", "vec.total_agents",
                    "train.total_timesteps"):
            section, name = key.split(".")
            self.assertEqual(config["sweep." + key]["min"], config[section][name])
            self.assertEqual(config["sweep." + key]["max"], config[section][name])
        varying = [s for s in config if s.startswith("sweep.") and config[s]["min"] != config[s]["max"]]
        self.assertEqual(len(varying), 10)

    def test_native_cli_declares_every_option(self):
        native = configparser.ConfigParser(interpolation=None)
        native.read([sweep.ROOT / "config/default.ini", sweep.ROOT / "config/pokemon.ini"])
        for section in sweep.read_config():
            for key in sweep.read_config()[section]:
                self.assertIn(key, native[section])

    def test_fixed_species_moves_and_lead(self):
        config = sweep.read_config()
        sets = json.loads((HERE / "data/catalog.json").read_text())["sets"]
        ids = list(map(int, config["env"]["learner_team"].split(",")))
        team = [sets[i] for i in ids]
        self.assertEqual([s["species"] for s in team],
                         ["Starmie", "Exeggutor", "Rhydon", "Chansey", "Snorlax", "Tauros"])
        self.assertTrue(all(len(s["move_ids"]) == 4 for s in team))
        self.assertEqual(int(config["env"]["learner_lead"]), team[0]["species_id"])

    def test_hardware_overrides_lock_matching_ranges(self):
        config = sweep.read_config(overrides=("--vec.total_agents=8192", "--vec.num_buffers=1"))
        self.assertEqual(sweep.learner_steps(config), 25_165_824)
        self.assertEqual(config["sweep.vec.total_agents"]["min"], "8192")
        config["train"]["minibatch_size"] = "1000"
        with self.assertRaises(ValueError):
            sweep.learner_steps(config)

    def test_all_searched_rollout_shapes_have_the_same_full_budget(self):
        for horizon in (32, 64):
            for minibatch in (8192, 16384, 32768):
                config = sweep.read_config(overrides=(f"--train.horizon={horizon}",
                                                     f"--train.minibatch_size={minibatch}"))
                self.assertEqual(sweep.learner_steps(config), 25_165_824)

    def test_scores_include_draws_and_require_complete_panel(self):
        rows = [{"mode": mode, "team": team, "games": 4, "wins": 1,
                 "draws": 2, "losses": 1, "timeouts": 1}
                for mode in ("argmax", "sample") for team in evaluation.PANEL]
        self.assertEqual(evaluation.aggregate(rows)["argmax"]["score"], .5)
        with self.assertRaises(ValueError):
            evaluation.aggregate(rows[:-1])

    def test_scores_do_not_replace_training_objective(self):
        config = sweep.read_config()
        self.assertEqual(config["sweep"]["metric"], "oak_score")
        self.assertEqual(config["base"]["load_model_path"], "None")
        self.assertEqual(config["base"]["checkpoint_interval"], "0")
        self.assertEqual(config["env"]["reward_win"], "1")
        self.assertEqual(config["env"]["reward_hp_scale"], "0")
        self.assertEqual(config["env"]["reward_ko_scale"], "0")


if __name__ == "__main__":
    unittest.main()
