import csv
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from kaggle_environments import make


KAG_DIR = Path(__file__).parents[1]
INDEXER = KAG_DIR / "index_replay_states.py"
BUILDER = KAG_DIR / "build_replay_state_bank.py"
LIB = KAG_DIR / "build" / "libkaggriculture.so"
HEADER = struct.Struct("<8sIIIIQ")


class ReplayStateBankTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", str(KAG_DIR), "lib"], check=True)

    def test_selected_states_are_parity_checked_and_resumable(self):
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            replay_path = tmp / "episode.json"
            index_path = tmp / "all.tsv"
            selected_path = tmp / "selected.tsv"
            bank_path = tmp / "states.kgb"

            env = make(
                "kaggriculture",
                configuration={"episodeSteps": 8, "seed": 7},
                debug=True,
            )
            env.run(["pass", "pass"])
            replay_path.write_text(json.dumps(env.toJSON()), encoding="utf-8")
            subprocess.run([
                sys.executable, str(INDEXER), str(replay_path),
                "--output", str(index_path),
            ], check=True, capture_output=True, text=True)

            with index_path.open(encoding="utf-8", newline="") as src:
                reader = csv.DictReader(src, delimiter="\t")
                rows = [row for row in reader if int(row["turn"]) in {0, 3, 6}]
                fields = reader.fieldnames
            self.assertEqual(len(rows), 6)
            with selected_path.open("w", encoding="utf-8", newline="") as dst:
                writer = csv.DictWriter(dst, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                writer.writerows(rows)

            subprocess.run([
                sys.executable, str(BUILDER), str(replay_path),
                "--index", str(selected_path),
                "--output", str(bank_path),
                "--lib", str(LIB),
            ], check=True, capture_output=True, text=True)

            raw = bank_path.read_bytes()
            magic, bank_version, state_version, state_size, count, reserved = HEADER.unpack_from(raw)
            self.assertEqual(magic, b"KGRSTB1\0")
            self.assertEqual(bank_version, 1)
            self.assertEqual(state_version, 1)
            self.assertEqual(count, 3)
            self.assertEqual(reserved, 0)
            self.assertEqual(len(raw), HEADER.size + count * state_size)

            with Path(f"{bank_path}.manifest.tsv").open(encoding="utf-8", newline="") as stream:
                manifest = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual([int(row["turn"]) for row in manifest], [0, 3, 6])
            self.assertTrue(all(row["players"] == "0,1" for row in manifest))
            self.assertTrue(all(len(row["sha256"]) == 64 for row in manifest))
            self.assertTrue(all(len(json.loads(row["index_rows"])) == 2 for row in manifest))

            summary = json.loads(Path(f"{bank_path}.summary.json").read_text())
            self.assertEqual(summary["record_count"], 3)
            self.assertEqual(summary["counts"]["resume_checks"], 3)
            self.assertGreaterEqual(summary["counts"]["parity_frames"], 7)


if __name__ == "__main__":
    unittest.main()
