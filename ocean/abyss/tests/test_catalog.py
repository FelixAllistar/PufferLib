"""Repair data plumbing and sweep configuration checks; no network or GPU."""
import configparser
import csv
import importlib.util
import itertools
import json
from pathlib import Path
import tempfile
import unittest

ENV = Path(__file__).resolve().parents[1]
ROOT = ENV.parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ENV / "tools" / f"{name}.py")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class CatalogTests(unittest.TestCase):
    def test_all_exported_repairers_keep_rate_layer_and_remote_range(self):
        with (ENV / "data/npc_stats.csv").open(encoding="utf-8-sig") as handle:
            rows = list(csv.DictReader(handle))
        catalog = {n["name"]: n for n in json.loads((ENV / "data/npc_catalog.json").read_text())}
        for row in rows:
            npc = catalog[row["type"]]
            for prefix, source in (("local", "Local Repair Per Second"), ("remote", "Remote Repair Per Second")):
                self.assertAlmostEqual(npc[f"{prefix}_repair_hp_per_s"], float(row[source] or 0))
                self.assertEqual(npc[f"{prefix}_repair_layer"] in (0, 1), float(row[source] or 0) > 0)
            self.assertEqual(npc["remote_repair_optimal_m"], float(row["rr_optimal"] or 0))
            self.assertEqual(npc["remote_repair_falloff_m"], float(row["rr_falloff"] or 0))

    def test_generated_files_reproduce(self):
        with tempfile.TemporaryDirectory() as tmp:
            catalog, header = Path(tmp)/"npcs.json", Path(tmp)/"scenarios.h"
            module("build_npc_catalog").build(ENV/"data/npc_stats.csv", catalog)
            self.assertEqual(catalog.read_bytes(), (ENV/"data/npc_catalog.json").read_bytes())
            module("build_scenario_catalog").build(ENV/"data/recorded/episodes.json", catalog,
                ENV/"data/trajectory_calibration.json", header)
            self.assertEqual(header.read_bytes(), (ENV/"generated_scenarios.h").read_bytes())

    def test_untyped_or_inconsistent_repair_is_not_silently_dropped(self):
        build = module("build_npc_catalog")
        with self.assertRaises(ValueError):
            build.repair_profile({"type": "bad", "Local Repair Per Second": "10"})
        with self.assertRaises(ValueError):
            build.repair_profile({"type": "bad", "local_rep_type": "Armor Repair",
                "local_rep_strength": "100", "local_rep_duration": "5", "Local Repair Per Second": "10"})

    def test_sweep_fixed_dimensions_and_rollout_shapes(self):
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.read([ROOT/"config/default.ini", ROOT/"config/abyss.ini"])
        varying = []
        for section in cfg.sections():
            if not section.startswith("sweep."): continue
            low, high = cfg.getfloat(section, "min"), cfg.getfloat(section, "max")
            self.assertLessEqual(low, high)
            target, key = section[6:].rsplit(".", 1)
            if low == high:
                self.assertEqual(low, cfg.getfloat(target, key), section)
            else:
                varying.append(section)
                self.assertLessEqual(low, cfg.getfloat(target, key), section)
                self.assertGreaterEqual(high, cfg.getfloat(target, key), section)
        self.assertEqual(len(varying), 9)
        for agents, horizon, batch in itertools.product((512,1024),(32,64,128),(1024,2048,4096)):
            self.assertEqual(batch % horizon, 0)
            self.assertLessEqual(batch, agents*horizon)
            self.assertEqual(agents % (batch//horizon), 0)
        self.assertEqual(cfg["base"]["load_model_path"], "None")


if __name__ == "__main__":
    unittest.main()
