"""Offline relocation/provenance tests using a local fake evaluator."""
import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from audit_archived_series import analyze_archive, resolve_log
from slumbot_series import analyze, fingerprint, freeze, run, write_new
from test_slumbot_series import FAKE_RUNNER


class ArchivedSeriesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        original = self.root / "original"
        original.mkdir()
        runner = original / "fake_eval.py"
        runner.write_text(FAKE_RUNNER.replace("DIRTY_SEED", "-1").replace("BAD_EXIT_SEED", "-1"))
        control, candidate = original / "control.so", original / "candidate.so"
        control.write_text("control")
        candidate.write_text("candidate")
        report = original / "reports" / "poker" / "confirmation_test"
        manifest = report / "manifest.json"
        args = argparse.Namespace(control=control, candidate=candidate, runner=runner,
                                  python=Path(sys.executable), output_dir=report / "confirmation",
                                  hands_per_job=4, games=5, start_seed=202610185000,
                                  control_samples=0, candidate_samples=0, contact="offline-test")
        write_new(manifest, freeze(args))
        with redirect_stdout(io.StringIO()):
            self.assertEqual(run(manifest), 0)
        self.original_result = analyze(manifest)
        self.original_hash = fingerprint(manifest)
        self.report_root = self.root / "relocated" / "reports"
        self.report_root.parent.mkdir()
        shutil.move(str(original / "reports"), self.report_root)
        shutil.rmtree(original)
        self.manifest = self.report_root / "poker" / "confirmation_test" / "manifest.json"

    def tearDown(self):
        self.temp.cleanup()

    def test_relocated_logs_reproduce_without_rewriting_manifest(self):
        result = analyze_archive(self.manifest, self.report_root,
                                 expected_manifest_sha256=self.original_hash)
        self.assertTrue(result["all_planned_jobs_complete_and_eligible"])
        self.assertEqual(fingerprint(self.manifest), self.original_hash)
        for key in ("arms", "primary_raw_difference", "secondary_adjusted_difference", "conclusion"):
            self.assertEqual(result[key], self.original_result[key])
        self.assertTrue(all(job["recorded_output"] != job["resolved_output"] for job in result["jobs"]))

    def test_changed_log_with_consistent_summary_still_fails_hash(self):
        path = self.manifest.parent / "confirmation" / "01_candidate.jsonl"
        events = [json.loads(line) for line in path.read_text().splitlines()]
        events[1]["winnings_chips"] += 100
        events[-1]["summary"]["net_bb"] += 1
        path.write_text("\n".join(json.dumps(event) for event in events) + "\n")
        result = analyze_archive(self.manifest, self.report_root)
        self.assertFalse(result["all_planned_jobs_complete_and_eligible"])
        self.assertIn("log SHA256 differs from completed-job provenance", result["jobs"][1]["errors"])
        self.assertEqual(result["arms"]["candidate"]["raw"]["hands"], 12)

    def test_missing_job_and_wrong_expected_manifest_cannot_confirm(self):
        (self.manifest.parent / "confirmation" / "07_candidate.jsonl").unlink()
        result = analyze_archive(self.manifest, self.report_root)
        self.assertFalse(result["all_planned_jobs_complete_and_eligible"])
        self.assertEqual(result["conclusion"], "incomplete_not_confirmatory")
        with self.assertRaisesRegex(ValueError, "expected SHA256"):
            analyze_archive(self.manifest, self.report_root, expected_manifest_sha256="0" * 64)

    def test_relative_resolution_rejects_traversal_and_symlink_escape(self):
        self.assertEqual(resolve_log('/old/reports/poker/run.jsonl', self.report_root),
                         self.report_root / 'poker' / 'run.jsonl')
        for recorded in ('/old/reports/../secrets', '/old/reports/reports/log.jsonl'):
            with self.subTest(recorded=recorded), self.assertRaises(ValueError):
                resolve_log(recorded, self.report_root)
        (self.report_root / 'escape').symlink_to(self.root)
        with self.assertRaises(ValueError):
            resolve_log('/old/reports/escape/outside.jsonl', self.report_root)


if __name__ == '__main__':
    unittest.main()
