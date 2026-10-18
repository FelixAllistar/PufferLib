"""Synthetic mathematical and selection gates; no match data or network."""
import hashlib
import math
import unittest

import numpy as np

from export_ranges import PAIRS, SERIES_SHA, holdout_partition
from fit_root_range import Objective, interval, nine_gates, predictions, score_data


class RootRangeTests(unittest.TestCase):
    def example(self):
        w = np.ones((10, PAIRS), dtype=np.float64)
        w[:, PAIRS // 2:] = 4.
        t = np.array([PAIRS - 1] * 6 + [0] * 4, dtype=np.int64)
        return w, t

    def test_analytic_derivative_and_interior_solution(self):
        w, t = self.example()
        objective = Objective(w, t)
        for alpha in (.1, .4, .8):
            h = 1e-5
            finite = (objective.value(alpha + h)[0] - objective.value(alpha - h)[0]) / (2 * h)
            analytic = 10 * math.log(4) * (4 ** alpha / (1 + 4 ** alpha) - .6) + 2 * (alpha - 1)
            self.assertAlmostEqual(objective.value(alpha)[1], analytic, places=12)
            self.assertAlmostEqual(finite, analytic, places=7)
        alpha, proof = objective.fit()
        self.assertGreater(alpha, 0)
        self.assertLess(alpha, 1)
        self.assertEqual(proof["updates"], 60)
        self.assertLess(abs(objective.value(alpha)[1]), 1e-12)
        self.assertLess(proof["selected_objective"], min(proof["objective_at_zero"], proof["objective_at_one"]))

    def test_true_endpoint_solutions_and_roundtrip(self):
        uniform = np.ones((8, PAIRS), dtype=np.float64)
        alpha, proof = Objective(uniform, np.zeros(8, dtype=int)).fit()
        self.assertEqual(alpha, 1.)
        self.assertEqual(proof["updates"], 0)
        w = np.tile(np.arange(1., PAIRS + 1), (8, 1))
        low, low_proof = Objective(w, np.zeros(8, dtype=int)).fit()
        high, high_proof = Objective(w, np.full(8, PAIRS - 1, dtype=int)).fit()
        self.assertEqual(low, 0.)
        self.assertEqual(high, 1.)
        self.assertGreaterEqual(low_proof["derivative_at_zero"], 0)
        self.assertLessEqual(high_proof["derivative_at_one"], 0)
        fit, _ = Objective(*self.example()).fit()
        self.assertEqual(float(format(fit, ".17g")), fit)

    def test_normalization_and_endpoint_comparator_identity(self):
        w, t = self.example()
        shares = np.zeros_like(w)
        shares[:, PAIRS // 2:] = 1.
        pred = predictions(w, t, shares, .4)
        expected = 4 ** .4 / (1 + 4 ** .4)
        self.assertTrue(np.allclose(pred["predicted_share"], expected, atol=1e-15, rtol=0))
        rows = [{"arm": "control" if i < 5 else "candidate", "call_chips": i % 2} for i in range(10)]
        for alpha, comparator in ((0., "uniform"), (1., "original")):
            report = score_data(rows, w, t, shares, alpha)
            for metric in ("pair_nll", "share_mse"):
                delta = report["groups"]["all"]["deltas"][f"fitted_minus_{comparator}_{metric}"]
                self.assertEqual(delta["mean"], 0.)
                self.assertEqual(delta["ci95"], [0., 0.])
            self.assertFalse(report["all_nine_passed"])

    def test_exact_nine_gate_strictness_and_missing_arm(self):
        names = ("fitted_minus_original_pair_nll", "fitted_minus_uniform_pair_nll", "fitted_minus_original_share_mse")
        good = interval([-.3, -.2, -.4, -.3])
        groups = {g: {"deltas": {n: dict(good) for n in names}} for g in ("all", "control", "candidate")}
        self.assertTrue(all(nine_gates(groups).values()))
        groups["candidate"]["deltas"][names[0]] = interval([0., 0.])
        self.assertTrue(all(nine_gates(groups).values()))
        groups["all"]["deltas"][names[0]] = interval([0., 0.])
        self.assertFalse(nine_gates(groups)["pooled_nll_vs_original_upper95_negative"])
        groups["all"]["deltas"][names[0]] = dict(good)
        groups["all"]["deltas"][names[2]] = interval([0., 0.])
        self.assertFalse(nine_gates(groups)["pooled_share_mse_vs_original_mean_negative"])
        groups["all"]["deltas"][names[2]] = dict(good)
        groups["candidate"]["deltas"][names[0]] = interval([-.5])
        self.assertFalse(all(nine_gates(groups).values()))
        self.assertEqual(len(nine_gates(groups)), 9)

    def test_hand_partition_uses_only_exact_frozen_identity(self):
        for job in ("00_control", "01_candidate"):
            for index in (0, 1, 9, 999, 3999):
                literal = "poker-root-range-shrinkage-holdout-v1:" + SERIES_SHA + ":" + job + ":" + str(index)
                expected = "val" if hashlib.sha256(literal.encode("ascii")).digest()[0] < 128 else "test"
                self.assertEqual(holdout_partition(job, index), expected)
                self.assertEqual(holdout_partition(job, index), holdout_partition(job, index))
        for bad in (-1, 1.5, "01", True):
            with self.assertRaises(ValueError):
                holdout_partition("00_control", bad)

    def test_one_hand_interval_matches_manual_pairing(self):
        values = [.1, -.4, -.2, .3]
        estimate = interval(values)
        mean = sum(values) / 4
        se = math.sqrt(sum((v - mean) ** 2 for v in values) / 12)
        self.assertEqual(estimate["n"], 4)
        self.assertAlmostEqual(estimate["mean"], mean)
        self.assertAlmostEqual(estimate["se"], se)
        self.assertFalse(interval([float("nan"), 0.])["eligible"])
        self.assertFalse(interval([])["eligible"])


if __name__ == "__main__":
    unittest.main()
