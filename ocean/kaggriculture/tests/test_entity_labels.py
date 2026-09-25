import pathlib
import sys
import unittest

import numpy as np

HERE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import build_entity_bc_dataset as dataset
import entity_bc_labels as labels
import multi_intent_labels
import replay_native as native


class LabelTests(unittest.TestCase):
    def test_multi_intent_labels_preserve_every_action_and_order(self):
        action = {"farmer": ["PLANT", "WHEAT"],
                  "hands": [["PLANT", "MELON"], ["FEED"], ["EAST"], ["PLACE", "COW", 1]],
                  "market": [["SELL", "MILK", 50], ["HIRE"], ["BUY_LAND"], ["BUY_SEED", "WHEAT", 12]]}
        annotation = multi_intent_labels.annotate(action, [[0, 0], [5, 0], [1, 1], [4, 4], [5, 5]],
                      episode_id="test", player=0, step=7, single_label=[-1, -1, -1], reason="conflict")
        self.assertEqual(annotation["primitive_action"], action)
        self.assertEqual([r["command"] for r in annotation["market_queue"]], action["market"])
        self.assertEqual([r["item"] for r in annotation["production_requests"]], ["WHEAT", "MELON"])
        self.assertEqual(annotation["strategic_request_count"], 6)
        self.assertEqual(len(annotation["chore_trace"]), 2)
        self.assertEqual(len(annotation["movement_trace"]), 1)
        annotation["primitive_action"]["farmer"][1] = "CARROT"
        self.assertEqual(action["farmer"][1], "WHEAT")

    def test_discounted_returns_and_terminal_mask(self):
        np.testing.assert_allclose(dataset.discounted_returns([1, 2, 3], .5),
                                   [2.75, 3.5, 3, np.nan], equal_nan=True)

    def test_quantityless_market_orders_are_not_dropped(self):
        action = native.c_action({"market": [["HIRE"], ["BUY_LAND"]]})
        self.assertEqual(labels.markets(action), {(4, -1): 1, (5, -1): 1})
        self.assertEqual(labels.candidates(action), [])  # cannot do both in one macro
        self.assertEqual(labels.candidates(native.c_action({"market": [["HIRE"]]})), [28])
        self.assertEqual(labels.candidates(native.c_action({"market": [["BUY_LAND"]]})), [9])

    def test_returning_cow_does_not_mean_buy_cow(self):
        action = native.c_action({"farmer": ["PLACE", "COW", 1]})
        self.assertEqual(labels.strategy(action), set())
        self.assertEqual(labels.candidates(action), [])

    def test_maintenance_is_not_disabled_macro_30(self):
        for command in ("WATER", "CARE", "FEED", "HARVEST"):
            self.assertEqual(labels.candidates(native.c_action({"farmer": [command]})), [])
        self.assertEqual(labels.candidates(native.c_action({"farmer": ["FERTILIZE"]})), [36])
        self.assertEqual(labels.candidates(native.c_action({"farmer": ["DIG"]})), [35])

    def test_conflicting_growth_is_not_guessed(self):
        action = native.c_action({"farmer": ["PLANT", "WHEAT"], "hands": [["PLANT", "MELON"]]})
        self.assertEqual(labels.candidates(action), [])
        self.assertEqual(labels.candidates(native.c_action({"farmer": ["BUILD_PASTURE"]})), [])

    def test_only_unique_arguments_are_supervised(self):
        action = native.c_action({"market": [["SELL", "MILK", 4]]})
        def decode(heads):
            return native.c_action({"market": [["SELL", "MILK", 4 if heads[1] == 2 else 1]]})
        label, history, reason = labels.project(action, np.ones(labels.MASK), decode)
        np.testing.assert_array_equal(label[:3], [16, 2, -1])
        np.testing.assert_array_equal(history[:3], [16, 2, 0])
        self.assertEqual(reason, "projected_strategy")

    def test_masked_macro_never_becomes_label(self):
        action = native.c_action({"market": [["HIRE"]]})
        label, _, reason = labels.project(action, np.zeros(labels.MASK), lambda _: action)
        self.assertTrue(np.all(label == -1))
        self.assertEqual(reason, "executor_cannot_reproduce_strategy")


if __name__ == "__main__":
    unittest.main()
