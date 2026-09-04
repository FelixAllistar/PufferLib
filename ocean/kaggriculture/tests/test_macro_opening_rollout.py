import unittest

from ocean.kaggriculture.evaluate_macro_opening_rollout import _farm_metrics


class MacroOpeningRolloutTests(unittest.TestCase):
    def test_farm_metrics_counts_public_assets(self):
        farm = {
            "money": 1234,
            "unlocked_quadrants": ["NW", "NE"],
            "hands": [[1, 1]],
            "tiles": [[
                None,
                "WEED",
                {"kind": "PLANT", "crop": "WHEAT"},
                {"kind": "PASTURE", "animal": "COW"},
                {"kind": "PASTURE"},
            ]],
        }
        metrics = _farm_metrics(farm, {"BUY_LAND": 1, "PLANT": 3})
        self.assertEqual(metrics["money"], 1234)
        self.assertEqual(metrics["land"], 2)
        self.assertEqual(metrics["plants"], 1)
        self.assertEqual(metrics["animals"], 1)
        self.assertEqual(metrics["structures"], 2)
        self.assertEqual(metrics["buy_land"], 1)
        self.assertEqual(metrics["plant"], 3)


if __name__ == "__main__":
    unittest.main()
