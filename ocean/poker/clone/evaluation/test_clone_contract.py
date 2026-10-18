"""Pure synthetic checks for V2 binding and whole-hand raw statistics."""
import math
import unittest

import clone_audit as audit
import clone_eval as runner


def binding():
    return {"model_version": 2, "size_domain": 2,
            "size_encoding": "legal_interior_fraction_f64_v2",
            "model_domain": "behavior-clone-full-model-v2",
            "model_npz_sha256": "1" * 64, "model_numeric_sha256": "2" * 64,
            "module_source_closure_sha256": "3" * 64, "module_binary_sha256": "4" * 64,
            "numeric_bytes": 227580, "nodes": 1924}


def rows():
    result = []
    for arm, values in (("control", (-1.0, 1.0)), ("candidate", (1.0, 3.0))):
        for hand, value in enumerate(values):
            result.append({"job": arm, "arm": arm, "hand": hand,
                "hand_reservation_index": hand, "actual_net_bb": value,
                "already_adjusted_net_bb": 0.0, "terminal_return_verified": True,
                "actual_native_decisions_checked": hand + 1,
                "terminal_trace_decisions_checked": hand + 1})
    return result


class ContractTests(unittest.TestCase):
    def test_binding_exact_schema_version_and_types(self):
        valid = binding()
        self.assertEqual(runner.validate_model_binding(valid), valid)
        bad = [dict(valid, model_version=1), dict(valid, size_domain=1),
               dict(valid, model_version=True), dict(valid, nodes=True),
               dict(valid, size_encoding="pot_log_ratio_v1"),
               dict(valid, model_npz_sha256="0" * 64), dict(valid, extra="unbound")]
        bad.append({key: value for key, value in valid.items() if key != "module_binary_sha256"})
        for item in bad:
            with self.assertRaises(runner.JobError):
                runner.validate_model_binding(item)

    def test_binding_digest_binds_values_and_not_mapping_order(self):
        valid = binding()
        first = runner.model_binding_digest(valid)
        self.assertEqual(first, runner.model_binding_digest(dict(reversed(list(valid.items())))))
        self.assertNotEqual(first, runner.model_binding_digest(dict(valid, model_numeric_sha256="5" * 64)))

    def test_raw_uses_all_hands_and_independent_arm_uncertainty(self):
        result = audit.summarize(rows(), 2)
        self.assertEqual(set(result["differences"]), {"raw", "already_adjusted"})
        self.assertEqual(result["arms"]["control"]["raw"]["bb100"], 0.0)
        self.assertEqual(result["arms"]["candidate"]["raw"]["bb100"], 200.0)
        raw = result["differences"]["raw"]
        self.assertEqual(raw["gain_bb100"], 200.0)
        self.assertAlmostEqual(raw["se_bb100"], math.sqrt(20000.0))
        self.assertAlmostEqual(raw["ci95_bb100"][0], 200.0 - 1.96 * math.sqrt(20000.0))
        self.assertEqual(result["differences"]["already_adjusted"]["gain_bb100"], 0.0)

    def test_missing_or_duplicate_hand_and_incomplete_replay_fail(self):
        cases = [rows()[:-1], rows() + [rows()[0]]]
        duplicate = rows(); duplicate[1]["hand_reservation_index"] = 0; cases.append(duplicate)
        incomplete = rows(); incomplete[2]["terminal_trace_decisions_checked"] = 0; cases.append(incomplete)
        for value in cases:
            with self.assertRaises(audit.AuditError):
                audit.summarize(value, 2)


if __name__ == "__main__":
    unittest.main()
