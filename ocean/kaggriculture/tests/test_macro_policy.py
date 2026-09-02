import sys
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

from macro_actions import candidate_actions
from macro_policy import build_observation, decision_payload, validate_decision


class MacroPolicyTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = {
            "step": 4,
            "farms": [
                {"money": 3000, "tiles": [[None]], "hands": []},
                {"money": 3000, "tiles": [[None]], "hands": []},
            ],
            "privates": [
                {"shed": {}, "seeds": {}, "inventories": []},
                {"shed": {"MILK": 9}, "seeds": {}, "inventories": []},
            ],
            "market": {"inventory": {}, "prices": {}}, "town": {},
        }
        self.candidates = candidate_actions(self.snapshot, 0, include_strategic=False)

    def test_observation_is_versioned_and_scores_are_aligned(self):
        observation = build_observation(
            self.snapshot, 0, self.candidates, [float(i) for i in range(len(self.candidates))]
        )
        self.assertEqual(observation["version"], 1)
        self.assertEqual(len(observation["candidates"]), len(observation["scores"]))
        self.assertEqual(observation["features"]["own_money"], 3000)

    def test_decision_rejects_invalid_indices_and_returns_plan(self):
        index, candidate = validate_decision({"candidate_index": 0}, self.candidates)
        payload = decision_payload(index, candidate)
        self.assertEqual(payload["candidate_id"], "HOLD")
        self.assertEqual(payload["version"], 1)
        with self.assertRaises(ValueError):
            validate_decision({"candidate_index": 999}, self.candidates)
        with self.assertRaises(ValueError):
            validate_decision({"candidate_index": "nope"}, self.candidates)


if __name__ == "__main__":
    unittest.main()

