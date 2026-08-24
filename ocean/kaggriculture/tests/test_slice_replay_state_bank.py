import csv
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


MODULE_DIR = Path(__file__).parents[1]
sys.path.insert(0, str(MODULE_DIR))

from build_replay_state_bank import BANK_FORMAT_VERSION, BANK_HEADER, BANK_MAGIC, MANIFEST_FIELDS
from slice_replay_state_bank import parse_args, slice_bank


class ReplayStateBankSliceTests(unittest.TestCase):
    def test_slice_preserves_only_matching_verified_records(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.kgb"
            manifest = Path(f"{source}.manifest.tsv")
            payloads = [b"a" * 8, b"b" * 8]
            with source.open("wb") as stream:
                stream.write(BANK_HEADER.pack(BANK_MAGIC, BANK_FORMAT_VERSION, 7, 8, 2, 0))
                for payload in payloads:
                    stream.write(payload)
            rows = []
            for idx, (payload, scenario) in enumerate(zip(payloads, ("sell_now", "early_expansion"))):
                rows.append({
                    "record_index": idx,
                    "byte_offset": BANK_HEADER.size + idx * 8,
                    "byte_size": 8,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "episode_id": idx,
                    "source": "fixture",
                    "module_version": "1.32.7",
                    "seed": 1,
                    "turn": idx,
                    "players": "0,1",
                    "state_keys": f"{idx}:0,{idx}:1",
                    "expert_actions": "[]",
                    "index_rows": json.dumps([{"scenarios": scenario}]),
                })
            with manifest.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=MANIFEST_FIELDS, delimiter="\t")
                writer.writeheader()
                writer.writerows(rows)

            output = root / "investment.kgb"
            summary = slice_bank(parse_args([
                "--bank", str(source), "--output", str(output), "--stage", "investment",
            ]))
            self.assertEqual(summary["record_count"], 1)
            with output.open("rb") as stream:
                header = BANK_HEADER.unpack(stream.read(BANK_HEADER.size))
                self.assertEqual(header[4], 1)
                self.assertEqual(stream.read(), payloads[1])


if __name__ == "__main__":
    unittest.main()
