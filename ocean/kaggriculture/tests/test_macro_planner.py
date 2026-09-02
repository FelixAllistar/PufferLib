import csv
import ctypes
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

from build_replay_state_bank import BANK_FORMAT_VERSION, BANK_HEADER, BANK_MAGIC
from macro_planner import evaluate, parse_args
from replay_native import CConfig, load_core


class MacroPlannerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import numpy  # noqa: F401
        except ImportError:
            cls.skip_reason = "numpy is optional for the planner model test"
            return
        cls.skip_reason = None
        cls.lib = load_core(KAG_DIR / "build" / "libkaggriculture.so")

    def test_mpc_writes_auditable_rows(self):
        if self.skip_reason:
            self.skipTest(self.skip_reason)
        import numpy as np

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = CConfig()
            self.lib.kg_config_default(ctypes.byref(config))
            config.episode_steps = 4
            state = self.lib.kg_create(ctypes.byref(config))
            try:
                size = int(self.lib.kg_state_serialized_size())
                version = int(self.lib.kg_state_serialization_version())
                payload_buffer = (ctypes.c_ubyte * size)()
                self.assertTrue(self.lib.kg_state_serialize(state, payload_buffer, size))
                payload = bytes(payload_buffer)
            finally:
                self.lib.kg_destroy(state)
            bank = root / "state.kgb"
            bank.write_bytes(BANK_HEADER.pack(
                BANK_MAGIC, BANK_FORMAT_VERSION, version, size, 1, 0
            ) + payload)
            manifest = Path(f"{bank}.manifest.tsv")
            fields = (
                "record_index", "byte_offset", "byte_size", "sha256", "episode_id",
                "source", "module_version", "seed", "turn", "players", "state_keys",
                "expert_actions", "index_rows",
            )
            with manifest.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                writer.writerow({
                    "record_index": "0", "byte_offset": str(BANK_HEADER.size),
                    "byte_size": str(size), "sha256": hashlib.sha256(payload).hexdigest(),
                    "episode_id": "fixture", "source": "fixture", "module_version": "1",
                    "seed": "0", "turn": "0", "players": "0,1", "state_keys": "",
                    "expert_actions": "[]", "index_rows": "[]",
                })
            names = np.asarray(["feature_step", "candidate_kind_code"])
            weights = np.asarray([0.0, 0.0, 0.01])
            model = root / "value.npz"
            np.savez_compressed(
                model, weights=weights, mean=np.zeros(2), scale=np.ones(2),
                feature_names=names, target=np.asarray(["delta_money"]),
            )
            output = root / "planner.tsv"
            summary = evaluate(parse_args([
                "--bank", str(bank), "--model", str(model), "--output", str(output),
                "--horizon", "2", "--search", "mpc", "--top-k", "2",
                "--opponent-mode", "pass", "--max-candidates", "5", "--direct-only",
            ]))
            self.assertEqual(summary["players"], 2)
            with output.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(rows), 2)
            self.assertIn("exact_candidates_json", rows[0])


if __name__ == "__main__":
    unittest.main()
