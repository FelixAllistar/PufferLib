import json
import sys
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

from state_sampling import row_scenarios, select_rows, time_bucket


def _row(index: int, turn: int, scenarios: str = "") -> dict[str, str]:
    index_rows = [{"scenarios": scenarios}] if scenarios else []
    return {
        "record_index": str(index), "episode_id": f"episode-{index}",
        "turn": str(turn), "index_rows": json.dumps(index_rows),
    }


class StateSamplingTests(unittest.TestCase):
    def test_time_buckets_cover_a_full_season(self):
        self.assertEqual(time_bucket(_row(0, 0)), "early")
        self.assertEqual(time_bucket(_row(1, 119)), "early")
        self.assertEqual(time_bucket(_row(2, 120)), "growth")
        self.assertEqual(time_bucket(_row(3, 239)), "growth")
        self.assertEqual(time_bucket(_row(4, 240)), "mid")
        self.assertEqual(time_bucket(_row(5, 479)), "mid")
        self.assertEqual(time_bucket(_row(6, 480)), "late")

    def test_scenario_tags_are_read_from_both_players(self):
        row = _row(0, 20, "early_expansion, tomato_opportunity")
        row["index_rows"] = json.dumps([
            {"scenarios": "early_expansion,tomato_opportunity"},
            {"scenarios": ["maintenance_profitable", "harvest_ready"]},
        ])
        self.assertEqual(
            row_scenarios(row),
            {"early_expansion", "tomato_opportunity", "maintenance_profitable", "harvest_ready"},
        )

    def test_stratified_selection_is_deterministic_and_covers_time(self):
        rows = [
            _row(index, turn, scenario)
            for index, (turn, scenario) in enumerate([
                (10, "early_expansion"), (25, "buy_opportunity"),
                (140, "medium_investment"), (180, "maintenance_profitable"),
                (300, "harvest_ready"), (420, "recovery"),
                (510, "liquidation_3d"), (650, "sell_now"),
            ])
        ]
        first = select_rows(rows, 4, strategy="stratified", seed=707)
        second = select_rows(rows, 4, strategy="stratified", seed=707)
        self.assertEqual([row["record_index"] for row in first], [row["record_index"] for row in second])
        self.assertEqual({time_bucket(row) for row in first}, {"early", "growth", "mid", "late"})

    def test_first_selection_preserves_legacy_order(self):
        rows = [_row(index, index) for index in range(10)]
        selected = select_rows(rows, 3, strategy="first")
        self.assertEqual([row["record_index"] for row in selected], ["0", "1", "2"])


if __name__ == "__main__":
    unittest.main()

