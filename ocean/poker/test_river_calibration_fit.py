"""Offline regression checks using synthetic data only; no match logs required."""
import contextlib
import io
import json
import math
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

import fit_river_calibration as calibration


def synthetic_row(index=0, label=1, split="validation"):
    # A hypothetical all-in response: calling is legal, re-raising is not.
    return {"job": "synthetic", "hand": index, "arm": "control" if index < 4 else "candidate",
            "split": split, "label": label, "observed_action": "f" if label == 0 else "c",
            "q_exact": .1, "price": .25, "call_chips": 100, "eligible_chips": 400,
            "proxy_raise_amount": 0, "pot_chips": 300,
            "opponent_stack_chips": 500, "hero_stack_chips": 0,
            "mc_q_replicas": [.1, .5] * 8, "mc_probabilities": [.5, .5, 0.],
            "exact_probabilities": [1., 0., 0.]}


class CalibrationTests(unittest.TestCase):
    def test_intercept_pseudocount_has_known_optimum(self):
        rows = [dict(synthetic_row(i, int(i >= 60), "train"), q_exact=.5) for i in range(100)]
        fit = calibration.fit_gate(rows, "fold")
        self.assertAlmostEqual(fit["coefficients"][0], math.log(60.5 / 40.5), places=10)
        self.assertEqual(fit["coefficients"][1:], [0., 0.])
        self.assertLess(fit["gradient_max_abs"], 1e-8)

    def test_clustered_interval_does_not_count_same_hand_twice(self):
        rows = [synthetic_row(0), synthetic_row(0), synthetic_row(1), synthetic_row(1)]
        got = calibration.cluster_mean([0., 0., 2., 2.], rows)
        # Two independent hands have means 0 and 2: SE of their mean is exactly 1.
        self.assertEqual(got["mean"], 1.)
        self.assertEqual(got["clustered_se"], 1.)

    def test_frozen_primary_scores_each_replica_before_averaging(self):
        coefficient = math.log(.8 / .2)
        model = {"phase": "frozen_train_fit", "gates": {
            "fold": {"coefficients": [coefficient, 0., 0.], "training_constant_probability": .8},
            "raise": {"coefficients": [0.] * 5, "training_constant_probability": .5},
            "bet": {"coefficients": [0.] * 4, "training_constant_probability": .5}}}
        with TemporaryDirectory() as directory:
            root = Path(directory)
            model_path, data_path, output = root / "model.json", root / "rows.jsonl", root / "scores.json"
            model_path.write_text(json.dumps(model))
            data_path.write_text("".join(json.dumps(synthetic_row(i)) + "\n" for i in range(8)))
            with contextlib.redirect_stdout(io.StringIO()):
                calibration.score(SimpleNamespace(model=model_path, data=data_path, output=output))
            result = json.loads(output.read_text())
        metrics = result["metrics"]["all"]
        expected_old = (-math.log(.001 / 1.003) - math.log(1.001 / 1.003)) / 2
        self.assertAlmostEqual(metrics["expected_replica_losses"]["old"]["logloss"], expected_old)
        # Averaging probabilities would reverse the direction for this fixture.
        self.assertGreater(metrics["paired_differences"]["calibrated_mc_minus_old_mc"]["logloss"]["mean"], 0)
        self.assertLess(metrics["expected_replica_paired_difference"]["logloss"]["mean"], 0)
        self.assertTrue(result["frozen_validation_criteria"]["mc_single_draw_logloss_ci_upper_negative"])
        self.assertNotIn("mc_mixture_logloss_ci_upper_negative", result["frozen_validation_criteria"])

    def test_illegal_labels_fail_before_scoring(self):
        cases = [dict(synthetic_row(label=0), call_chips=0),
                 dict(synthetic_row(), label=2, observed_action="b200"),
                 dict(synthetic_row(), observed_action="k"),
                 dict(synthetic_row(), call_chips=0)]
        with TemporaryDirectory() as directory:
            path = Path(directory) / "rows.jsonl"
            for row in cases:
                path.write_text(json.dumps(row) + "\n")
                with self.assertRaises(ValueError):
                    calibration.read_rows(path)

    def test_validation_labels_cannot_fit_parameters(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            plan, data = root / "plan.json", root / "rows.jsonl"
            plan.write_text(json.dumps({"regularization": {"ridge_lambda_nonintercept": 2.},
                                        "candidate_models_planned": 1}))
            data.write_text(json.dumps(synthetic_row()) + "\n")
            with self.assertRaisesRegex(ValueError, "Only TRAIN"):
                calibration.fit(SimpleNamespace(plan=plan, data=data, output=root / "model.json"))
            self.assertFalse((root / "model.json").exists())


if __name__ == "__main__":
    unittest.main()
