import importlib.util
import json
from pathlib import Path
import random
import sys
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "qd_sweep.py"
SPEC = importlib.util.spec_from_file_location("kag_qd_sweep", MODULE_PATH)
qd = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = qd
SPEC.loader.exec_module(qd)


class QualityDiversitySweepTests(unittest.TestCase):
    def test_behavior_niches_are_outcome_based(self):
        descriptor = qd.behavior_descriptor({
            "env/land_purchases": 2.1,
            "env/crop_production_units": 60,
            "env/animal_production_units": 40,
            "env/purchase_spend": 50_000,
            "env/sales_revenue": 100_000,
        })
        self.assertEqual(qd.niche_key(descriptor), "partial/animal_led/balanced")
        self.assertAlmostEqual(descriptor["animal_fraction"], 0.4)
        self.assertAlmostEqual(descriptor["reinvestment"], 0.5)

    def test_archive_keeps_best_cash_inside_each_niche(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = qd.Archive(Path(temporary) / "archive.json")
            base = {"niche": "compact/crop/cash_heavy", "parameters": {},
                    "descriptor": {"land": 0, "animal_fraction": 0,
                                   "reinvestment": 0}}
            self.assertTrue(archive.consider({**base, "quality": 10, "money": 10}))
            self.assertFalse(archive.consider({**base, "quality": 9, "money": 9}))
            self.assertTrue(archive.consider({**base, "quality": 11, "money": 11}))
            self.assertEqual(archive.entries[base["niche"]]["money"], 11)

    def test_journal_reindex_recomputes_derived_niche(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = qd.Archive(root / "archive.json")
            record = {
                "niche": "obsolete", "quality": 1, "money": 1,
                "parameters": {}, "checkpoint": "checkpoint.bin",
                "trial": 0, "family": "test", "metrics": {
                    "env/money": 20_000, "env/land_purchases": 3,
                    "env/crop_production_units": 50,
                    "env/animal_production_units": 50,
                    "env/purchase_spend": 20_000,
                    "env/sales_revenue": 100_000,
                },
            }
            journal = root / "trials.jsonl"
            journal.write_text(json.dumps({"trial": 0, "records": [record]}) + "\n")
            self.assertEqual(qd.reindex_archive_from_journal(archive, journal), 1)
            self.assertIn("full/animal_led/cash_heavy", archive.entries)

    def test_eval_json_parser_ignores_dashboard_noise(self):
        payload = {"env/money": 42_000.5, "env/win_rate": 0.75}
        text = "dashboard junk\n" + json.dumps(payload) + "\n"
        self.assertEqual(qd.parse_eval_json(text), payload)

    def test_parameter_samples_remain_in_declared_ranges(self):
        rng = random.Random(7)
        for family in qd.DEFAULT_FAMILIES:
            for global_sample in (False, True):
                sample = qd.sample_parameters(rng, family,
                                              global_sample=global_sample)
                self.assertEqual(set(sample), set(qd.SPECS))
                for name, value in sample.items():
                    spec = qd.SPECS[name]
                    self.assertGreaterEqual(value, spec.low)
                    self.assertLessEqual(value, spec.high)
                    if spec.integer:
                        self.assertIsInstance(value, int)

    def test_animal_family_survives_archive_parent_mutation(self):
        parent = qd.sample_parameters(random.Random(1), "crop", global_sample=False)
        child = qd.sample_parameters(
            random.Random(2), "animal", global_sample=False, parent=parent,
            mutation_strength=0.0)
        self.assertGreater(child["env.reward_progress_animal_scale"],
                           child["env.reward_progress_crop_scale"])
        self.assertGreater(child["env.reward_progress_animal_scale"],
                           parent["env.reward_progress_animal_scale"])


if __name__ == "__main__":
    unittest.main()
