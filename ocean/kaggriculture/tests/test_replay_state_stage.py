import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


KAG_DIR = Path(__file__).parents[1]
SELECTOR = KAG_DIR / "select_replay_state_stage.py"
FIELDS = (
    "state_key", "episode_id", "source", "module_version", "seed", "player",
    "agent", "final_money", "winner", "turn", "day", "hour",
    "remaining_turns", "remaining_days", "money", "product_units",
    "seed_units", "plants", "animals", "weeds", "maintenance_due",
    "unlocked_tiles", "carrot_price_ratio", "tomato_price_ratio",
    "egg_price_ratio", "expert_unit_ops", "expert_market_ops", "scenarios",
)


class ReplayStateStageTests(unittest.TestCase):
    def test_balances_states_and_preserves_both_rows(self):
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            index = tmp / "index.tsv"
            output = tmp / "sell.tsv"
            with index.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t")
                writer.writeheader()
                for turn in range(8):
                    for player in range(2):
                        row = {field: "0" for field in FIELDS}
                        row.update({
                            "state_key": f"e:{turn}:{player}",
                            "episode_id": "e", "source": "episode.json",
                            "player": str(player), "turn": str(turn),
                            "final_money": "12000",
                            "winner": "1" if player == 0 else "0",
                            "scenarios": "sell_now,liquidation_1d" if player == 0 else "",
                        })
                        writer.writerow(row)
            subprocess.run([
                sys.executable, str(SELECTOR), "--index", str(index),
                "--output", str(output), "--stage", "sell",
                "--max-states-per-scenario", "3", "--seed", "7",
            ], check=True, capture_output=True, text=True)
            with output.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(rows), 6)
            counts = {}
            for row in rows:
                counts.setdefault(int(row["turn"]), set()).add(int(row["player"]))
            self.assertEqual(len(counts), 3)
            self.assertTrue(all(players == {0, 1} for players in counts.values()))
            summary = json.loads(Path(f"{output}.summary.json").read_text())
            self.assertEqual(summary["selected_states"], 3)
            self.assertEqual(summary["sampled_per_scenario"]["sell_now"], 3)


if __name__ == "__main__":
    unittest.main()
