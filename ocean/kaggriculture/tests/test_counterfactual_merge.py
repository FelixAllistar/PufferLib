import csv
import sys
import tempfile
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

from merge_counterfactual import merge


class CounterfactualMergeTests(unittest.TestCase):
    def test_heap_merge_preserves_state_groups(self):
        fields = ["record_index", "player", "candidate_id"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shards = []
            for index, rows in enumerate((
                [(0, 0, "HOLD"), (2, 0, "HOLD")],
                [(0, 1, "HOLD"), (1, 0, "HOLD")],
            )):
                path = root / f"shard{index}.tsv"
                with path.open("w", encoding="utf-8", newline="") as stream:
                    writer = csv.writer(stream, delimiter="\t")
                    writer.writerow(fields)
                    writer.writerows(rows)
                shards.append(path)
            output = root / "merged.tsv"
            summary = merge(shards, output)
            self.assertEqual(summary["rows"], 4)
            with output.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(
                [(int(row["record_index"]), int(row["player"])) for row in rows],
                [(0, 0), (0, 1), (1, 0), (2, 0)],
            )


if __name__ == "__main__":
    unittest.main()
