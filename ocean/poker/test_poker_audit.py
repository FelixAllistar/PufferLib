"""Meaningful integrity and inference checks for the offline audit harness."""
import json
import math
from pathlib import Path
import tempfile
import unittest

from poker_audit import compare_blocks, compare_logs, parse_log


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, returns, *, final=True, interrupted_after=None, target=None, baseline=None,
              semantics=None, suffix=b"", seed=1):
        metadata = {"style": 7, "equity_samples": 128, "library_sha256": name,
                    "opponent": "slumbot.com", "starting_bb": 200,
                    "seed": seed, "hands_requested": target or len(returns), "log_schema": 3}
        if semantics:
            metadata["baseline_winnings_semantics"] = semantics
        events = [{"metadata": metadata}]
        unresolved = 0
        for i, value in enumerate(returns):
            if i == interrupted_after:
                events.append({"interrupted": {"game": 0, "stage": "act", "type": "Timeout", "decisions": []}})
                unresolved += 1
            record = {"hand": i, "position": i % 2, "game": 0, "winnings_chips": 100 * value,
                      "action": "b200f", "decisions": []}
            if baseline is not None and baseline[i] is not None:
                record["baseline_winnings_chips"] = 100 * baseline[i]
            events.append(record)
        if final:
            events.append({"summary": {"hands": len(returns), "unresolved_hands": unresolved,
                           "net_bb": sum(returns), "active_games": 0,
                           "target_reached": len(returns) == metadata["hands_requested"],
                           "complete": not unresolved and len(returns) == metadata["hands_requested"]}})
        path = self.directory / name
        path.write_bytes(("\n".join(json.dumps(x) for x in events) + "\n").encode() + suffix)
        return path

    def test_clean_mean_and_ci(self):
        result = parse_log(self.write("clean", [-1, 0, 1]))
        self.assertEqual(result["estimate"]["bb100"], 0)
        self.assertAlmostEqual(result["estimate"]["ci95_half_width_bb100"], 196 / math.sqrt(3))
        self.assertTrue(result["window"]["clean"])
        self.assertEqual(result["audit"]["source_state"], "target_completed_clean")

    def test_forced_stop_unknown_even_with_completed_hands(self):
        result = parse_log(self.write("killed", [-1, 1], final=False))
        self.assertTrue(result["window"]["pending_outcomes_unknown"])
        self.assertFalse(result["window"]["clean"])
        self.assertIsNone(result["window"]["missing_outcome_extreme_mean_bounds_bb100"])

    def test_graceful_stop_is_clean_but_partial(self):
        result = parse_log(self.write("graceful", [-1, 1], target=10))
        self.assertTrue(result["window"]["clean"])
        self.assertEqual(result["audit"]["source_state"], "graceful_stop_clean_partial")

    def test_interrupted_mean_bounds_and_prefix(self):
        path = self.write("interrupted", [-1, 1, 0], interrupted_after=2)
        full = parse_log(path)
        self.assertFalse(full["window"]["clean"])
        self.assertEqual(full["window"]["missing_outcome_extreme_mean_bounds_bb100"], [-5000, 5000])
        prefix = parse_log(path, prefix_hands=2)
        self.assertTrue(prefix["window"]["clean"])
        self.assertEqual(prefix["window"]["unresolved_attempts"], 0)
        self.assertTrue(prefix["window"]["requires_outcome_independent_prefix_plan"])

    def test_truncated_tail_overrides_old_summary(self):
        result = parse_log(self.write("truncated", [-1, 1], suffix=b'{"winnings_'))
        self.assertFalse(result["window"]["clean"])
        self.assertTrue(result["window"]["pending_outcomes_unknown"])
        self.assertEqual(len(result["audit"]["parse_errors"]), 1)

    def test_resume_does_not_erase_unknown_prior_segment(self):
        path = self.write("resumed_kill", [-1, 1], final=False)
        metadata = json.loads(path.read_text().splitlines()[0])["metadata"]
        with path.open("a") as handle:
            handle.write(json.dumps({"resumed": metadata}) + "\n")
            handle.write(json.dumps({"summary": {"hands": 2, "unresolved_hands": 0,
                                                    "net_bb": 0, "active_games": 0,
                                                    "target_reached": True, "complete": True}}) + "\n")
        result = parse_log(path)
        self.assertFalse(result["window"]["clean"])
        self.assertTrue(result["audit"]["unknown_prior_segments"])

    def test_comparison_uses_both_arm_uncertainties(self):
        control = self.write("a", [-1, 0, 1])
        candidate = self.write("b", [0, 1, 2], seed=2)
        result = compare_logs(control, candidate)
        self.assertEqual(result["difference"]["gain_bb100"], 100)
        self.assertAlmostEqual(result["difference"]["ci95_half_width_bb100"], 196 * math.sqrt(2 / 3))
        confirmed = compare_logs(control, candidate, phase="confirmation")
        self.assertEqual(confirmed["assessment"], "not_eligible")

    def test_baseline_already_adjusted_is_not_subtracted_twice(self):
        path = self.write("adjusted", [-80, 200, -20], baseline=[120, 0, -1],
                          semantics="already_adjusted_hero_minus_slumbot_selfplay")
        result = parse_log(path, baseline_mode="already_adjusted")
        self.assertAlmostEqual(result["server_baseline"]["server_relative_estimate"]["net_bb"], 119)
        with self.assertRaises(ValueError):
            parse_log(path, baseline_mode="raw_reference")

    def test_baseline_semantics_required_and_missing_coverage(self):
        unknown = self.write("unknown", [-1, 1], baseline=[0, 0])
        with self.assertRaises(ValueError):
            parse_log(unknown, baseline_mode="already_adjusted")
        control = self.write("a", [-1, 0, 1], baseline=[0, 1, 2],
                             semantics="already_adjusted_hero_minus_slumbot_selfplay")
        partial = self.write("b", [0, 1, 2], baseline=[None, 1, 2],
                             semantics="already_adjusted_hero_minus_slumbot_selfplay", seed=2)
        result = compare_logs(control, partial, baseline_mode="already_adjusted", metric="server_relative")
        self.assertIn("candidate lacks full baseline coverage", result["eligibility_blockers"])

    def test_local_pairs_by_id_and_rejects_missing_blocks(self):
        a, b = self.directory / "a.csv", self.directory / "b.csv"
        a.write_text("block,mean_bb\n0,-2\n1,0\n2,2\n")
        b.write_text("block,mean_bb\n2,3\n0,-1\n1,1\n")
        result = compare_blocks(a, b)
        self.assertEqual(result["difference"]["gain_bb100"], 100)
        self.assertEqual(result["difference"]["ci95_half_width_bb100"], 0)
        b.write_text("block,mean_bb\n0,1\n1,1\n")
        with self.assertRaises(ValueError):
            compare_blocks(a, b)

    def test_seed_reservations_require_unique_terminal_resolution(self):
        path = self.write("reserved", [-1, 1])
        records = [json.loads(line) for line in path.read_text().splitlines()]
        records[0]["metadata"].update(log_schema=4, policy_rng_mode="independent_hand_seeds_v1")
        events = [records[0]]
        for index, record in enumerate(records[1:-1]):
            record["hand_reservation_index"] = index
            events.extend([{"hand_reserved": {"hand_reservation_index": index}}, record])
        events.append(records[-1])
        save = lambda rows: path.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
        save(events)
        self.assertTrue(parse_log(path)["window"]["clean"])
        # A claimed drained summary cannot erase a reservation with no outcome.
        save(events[:-1] + [{"hand_reserved": {"hand_reservation_index": 2}}, events[-1]])
        audit = parse_log(path)
        self.assertFalse(audit["window"]["clean"])
        self.assertTrue(audit["window"]["pending_outcomes_unknown"])
        # Nor can a duplicate terminal record resolve the same index twice.
        duplicate = json.loads(json.dumps(events))
        duplicate[4]["hand_reservation_index"] = 0
        save(duplicate)
        self.assertFalse(parse_log(path)["window"]["clean"])


if __name__ == "__main__":
    unittest.main()
