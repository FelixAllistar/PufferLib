import sys
import tempfile
import unittest
from pathlib import Path


KAG_DIR = Path(__file__).parents[1]
sys.path.insert(0, str(KAG_DIR))

from fit_macro_value import _effective_rows, _rank_labels, _split_rows, feature_names
from macro_actions import candidate_actions
from macro_value_model import MacroValueModel, choose_best


class MacroValueContractTests(unittest.TestCase):
    def test_effective_filter_keeps_hold_reference_and_drops_rejected_actions(self):
        rows = [
            {"candidate_kind": "HOLD", "candidate_effective": "0", "delta_money": "0"},
            {"candidate_kind": "SELL", "candidate_effective": "0", "delta_money": "0"},
            {"candidate_kind": "BUY_LAND", "candidate_effective": "1", "delta_money": "-1000"},
        ]
        kept, dropped = _effective_rows(rows, ["candidate_kind", "candidate_effective"])
        self.assertEqual(dropped, 1)
        self.assertEqual([row["candidate_kind"] for row in kept], ["HOLD", "BUY_LAND"])

    def test_rank_labels_stay_in_lightgbm_relevance_range(self):
        rows = [
            {"state_id": "s", "delta_money": str(value)}
            for value in range(60)
        ]
        labels = _rank_labels(rows, "delta_money")
        self.assertEqual(int(labels.min()), 0)
        self.assertEqual(int(labels.max()), 30)

    def test_feature_columns_are_fixed_and_candidate_codes_are_appended(self):
        fields = ["episode_id", "feature_step", "feature_own_money",
                  "candidate_kind_code", "candidate_quantity", "delta_money",
                  "ignored_text"]
        self.assertEqual(
            feature_names(fields),
            ["feature_step", "feature_own_money", "candidate_kind_code",
             "candidate_quantity"],
        )

    def test_episode_split_is_stable_and_does_not_leak_groups(self):
        rows = [
            {"episode_id": "a", "delta_money": "1"},
            {"episode_id": "a", "delta_money": "2"},
            {"episode_id": "e", "delta_money": "3"},
            {"episode_id": "e", "delta_money": "4"},
            {"episode_id": "g", "delta_money": "5"},
            {"episode_id": "g", "delta_money": "6"},
        ]
        train_a, validation_a = _split_rows(rows, 0.5, 707)
        train_b, validation_b = _split_rows(rows, 0.5, 707)
        self.assertEqual(train_a, train_b)
        self.assertEqual(validation_a, validation_b)
        self.assertTrue(train_a and validation_a)
        self.assertTrue({row["episode_id"] for row in train_a}.isdisjoint(
            row["episode_id"] for row in validation_a
        ))

    def test_single_episode_split_is_rejected(self):
        rows = [{"episode_id": "only"}, {"episode_id": "only"}]
        with self.assertRaises(ValueError):
            _split_rows(rows, 0.2, 707)

    def test_ridge_model_scores_candidates_in_saved_feature_order(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("numpy is optional for the dependency-light contract tests")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "value.npz"
            names = np.asarray(["feature_step", "candidate_kind_code"])
            # The candidate-kind coefficient makes BUY_LAND beat HOLD in this
            # synthetic scorer; this checks loading, standardization, and ties.
            np.savez_compressed(
                path, weights=np.asarray([0.0, 0.0, 1.0]),
                mean=np.asarray([0.0, 0.0]), scale=np.asarray([1.0, 1.0]),
                feature_names=names, target=np.asarray(["delta_money"]),
            )
            model = MacroValueModel.load(path)
            snapshot = {
                "step": 4, "farms": [{"money": 3000, "tiles": [[None]], "hands": []},
                                     {"money": 3000, "tiles": [[None]], "hands": []}],
                "privates": [{"shed": {}, "seeds": {}, "inventories": []},
                              {"shed": {}, "seeds": {}, "inventories": []}],
                "market": {"inventory": {}, "prices": {}}, "town": {},
            }
            candidates = candidate_actions(snapshot, 0, include_strategic=False)
            scores = model.predict_candidates(snapshot, 0, candidates)
            index, candidate, score = choose_best(candidates, scores)
            self.assertEqual(candidate.kind, "HIRE")
            self.assertEqual(index, next(i for i, item in enumerate(candidates) if item.kind == "HIRE"))
            self.assertEqual(score, 6.0)


if __name__ == "__main__":
    unittest.main()
