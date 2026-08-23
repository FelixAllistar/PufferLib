import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).parents[1] / "index_replay_states.py"
SPEC = importlib.util.spec_from_file_location("index_replay_states", MODULE_PATH)
INDEX = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(INDEX)


def observation(*, tomato=60, carrot=35, egg=50, product_units=0, neglected=False):
    tile = {
        "kind": "PLANT",
        "crop": "WHEAT",
        "watered_today": not neglected,
        "consecutive_unwatered": int(neglected),
    }
    return {
        "day": 10,
        "hour": 0,
        "farms": [
            {"money": 3000, "tiles": [[tile]]},
            {"money": 3000, "tiles": [["LOCKED"]]},
        ],
        "private": {
            "shed": {"WHEAT": product_units},
            "seeds": {"WHEAT": 2},
            "inventories": [],
        },
        "market": {"prices": {"CARROT": carrot, "TOMATO": tomato, "EGG": egg}},
    }


class ReplayStateIndexTests(unittest.TestCase):
    def test_counterfactual_price_changes_only_matching_opportunity(self):
        ordinary, scenarios_a, _, _ = INDEX.classify_state(
            observation(), {"farmer": ["PASS"], "hands": [], "market": []}, 0, 240, 24
        )
        expensive, scenarios_b, _, _ = INDEX.classify_state(
            observation(tomato=120), {"farmer": ["PASS"], "hands": [], "market": []}, 0, 240, 24
        )
        self.assertNotIn("tomato_opportunity", scenarios_a)
        self.assertIn("tomato_opportunity", scenarios_b)
        self.assertEqual(ordinary["carrot_price_ratio"], expensive["carrot_price_ratio"])
        self.assertEqual(expensive["tomato_price_ratio"], 2.0)

    def test_sell_hold_maintenance_and_liquidation_tags(self):
        action = {
            "farmer": ["WATER"],
            "hands": [["HARVEST"]],
            "market": [["SELL", "WHEAT", 1]],
        }
        facts, scenarios, unit_ops, market_ops = INDEX.classify_state(
            observation(product_units=3, neglected=True), action, 0, 20, 24
        )
        self.assertEqual(facts["product_units"], 3)
        self.assertEqual(facts["maintenance_due"], 1)
        self.assertIn("sell_now", scenarios)
        self.assertNotIn("hold_for_later", scenarios)
        self.assertIn("maintenance_profitable", scenarios)
        self.assertIn("harvest_ready", scenarios)
        self.assertIn("liquidation_1d", scenarios)
        self.assertEqual(unit_ops, ["WATER", "HARVEST"])
        self.assertEqual(market_ops, ["SELL"])

    def test_replay_actions_are_aligned_from_next_record(self):
        obs = observation(product_units=1)
        episode = {
            "name": "kaggriculture",
            "module_version": "1.32.7",
            "configuration": {"turnsPerDay": 24},
            "statuses": ["DONE", "DONE"],
            "rewards": [5000, 4000],
            "info": {"EpisodeId": 7, "TeamNames": ["A", "B"], "seed": 11},
            "steps": [
                [
                    {"observation": obs, "action": {"farmer": ["PASS"], "hands": [], "market": []}},
                    {"observation": observation(), "action": {"farmer": ["PASS"], "hands": [], "market": []}},
                ],
                [
                    {"observation": obs, "action": {"farmer": ["PASS"], "hands": [], "market": [["SELL", "WHEAT", 1]]}},
                    {"observation": observation(), "action": {"farmer": ["PASS"], "hands": [], "market": []}},
                ],
            ],
        }
        rows = list(INDEX.episode_rows("fixture", episode))
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["state_key"], "7:0:0")
        self.assertEqual(rows[0]["expert_market_ops"], "SELL")
        self.assertIn("sell_now", rows[0]["scenarios"].split(","))


if __name__ == "__main__":
    unittest.main()
