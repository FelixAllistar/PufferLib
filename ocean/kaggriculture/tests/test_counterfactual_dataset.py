import csv
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


KAG_DIR = Path(__file__).parents[1]
LIB = KAG_DIR / "build" / "libkaggriculture.so"
sys.path.insert(0, str(KAG_DIR))

from build_replay_state_bank import BANK_FORMAT_VERSION, BANK_HEADER, BANK_MAGIC
from counterfactual_dataset import generate, parse_args
from macro_actions import FEATURE_FIELDS, candidate_actions, public_features
from replay_native import CConfig, c_snapshot, load_core


class CounterfactualDatasetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", str(KAG_DIR), "lib"], check=True)
        cls.lib = load_core(LIB)

    def test_public_features_never_depend_on_opponent_private_state(self):
        snapshot = {
            "step": 10, "day": 0, "hour": 10,
            "farms": [
                {"money": 4000, "hands": [], "unlocked_quadrants": ["NW"],
                 "tiles": [[{"kind": "PLANT", "watered_today": True}]]},
                {"money": 5000, "hands": [], "unlocked_quadrants": ["NW"],
                 "tiles": [[{"kind": "WEED"}]]},
            ],
            "privates": [
                {"shed": {"MILK": 2}, "seeds": {"WHEAT": 3}, "inventories": []},
                {"shed": {"MILK": 999999}, "seeds": {"WHEAT": 999999}, "inventories": []},
            ],
            "market": {"inventory": {"WHEAT": 100}, "prices": {"WHEAT": 25}},
            "town": {"unlocked_shops": []},
        }
        first = public_features(snapshot, 0)
        snapshot["privates"][1]["shed"]["MILK"] = 1
        snapshot["privates"][1]["seeds"]["WHEAT"] = 1
        second = public_features(snapshot, 0)
        self.assertEqual(first, second)
        self.assertEqual(set(FEATURE_FIELDS), set(first))

    def test_catalog_has_hold_and_only_directly_executable_actions(self):
        snapshot = {
            "farms": [{"tiles": [[None]], "unlocked_quadrants": []}, {"tiles": [[None]]}],
            "privates": [
                {"shed": {"MILK": 6}, "seeds": {}, "inventories": []},
                {"shed": {}, "seeds": {}, "inventories": []},
            ],
            "market": {"inventory": {}, "prices": {}}, "town": {"unlocked_shops": []},
        }
        actions = candidate_actions(snapshot, 0)
        self.assertEqual(actions[0].action_id, "HOLD")
        self.assertTrue(any(item.action_id == "SELL:MILK:6" for item in actions))
        self.assertTrue(all(item.executable for item in actions))
        self.assertTrue(all(item.payload()["farmer"] == ["PASS"] for item in actions))

    def test_terminal_counterfactual_uses_common_baseline_and_real_cash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bank_path = root / "state.kgb"
            manifest_path = Path(f"{bank_path}.manifest.tsv")
            config = CConfig()
            self.lib.kg_config_default(ctypes.byref(config))
            config.episode_steps = 2
            config.seed = 7
            state = self.lib.kg_create(ctypes.byref(config))
            try:
                size = int(self.lib.kg_state_serialized_size())
                version = int(self.lib.kg_state_serialization_version())
                payload_buffer = (ctypes.c_ubyte * size)()
                self.assertTrue(self.lib.kg_state_serialize(state, payload_buffer, size))
                payload = bytes(payload_buffer)
                snapshot = c_snapshot(self.lib, state)
            finally:
                self.lib.kg_destroy(state)

            with bank_path.open("wb") as stream:
                stream.write(BANK_HEADER.pack(
                    BANK_MAGIC, BANK_FORMAT_VERSION, version, size, 1, 0
                ))
                stream.write(payload)
            row = {
                "record_index": "0", "byte_offset": str(BANK_HEADER.size),
                "byte_size": str(size), "sha256": hashlib.sha256(payload).hexdigest(),
                "episode_id": "fixture", "source": "fixture", "module_version": "1.32.7",
                "seed": "7", "turn": "0", "players": "0,1", "state_keys": "fixture:0:0,fixture:0:1",
                "expert_actions": "[]", "index_rows": "[]",
            }
            fields = tuple(row)
            with manifest_path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                writer.writerow(row)

            output = root / "counterfactual.tsv"
            summary = generate(parse_args([
                "--bank", str(bank_path), "--output", str(output),
                "--lib", str(LIB), "--horizon", "terminal", "--players", "0",
            ]))
            self.assertEqual(summary["counts"]["states"], 1)
            self.assertEqual(summary["counts"]["players"], 1)
            self.assertGreater(summary["counts"]["candidates"], 1)
            with output.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            hold = next(item for item in rows if item["candidate_id"] == "HOLD")
            self.assertEqual(float(hold["delta_money"]), 0.0)
            self.assertEqual(int(hold["horizon_terminal"]), 1)
            land = next(item for item in rows if item["candidate_id"] == "BUY_LAND")
            self.assertEqual(float(land["candidate_final_money"]), 2000.0)
            self.assertEqual(float(land["delta_money"]), -1000.0)
            self.assertEqual(int(land["candidate_effective"]), 1)
            self.assertEqual(int(land["candidate_terminal"]), 1)
            self.assertEqual(len(rows[0]), len(set(rows[0])))


if __name__ == "__main__":
    unittest.main()

