"""CPU-only checks for the immutable roster evaluation's cached-result guard."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / 'benchmarks/evaluate_roster_snapshot.py'
SPEC = importlib.util.spec_from_file_location('evaluate_roster_snapshot', SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SnapshotSummaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / 'summary.json'
        self.member = {'sha256': 'checkpoint-hash'}
        self.protocol = {'games': 128, 'budget': 4096, 'seed': 20261015}
        self.summary = {
            'sha256': self.member['sha256'], 'budget': 4096, 'seed': 20261015,
            'scores': {mode: {'games': 384, 'wins': 100, 'draws': 2,
                              'losses': 282, 'score': 101 / 384}
                       for mode in ('argmax', 'sample')},
        }

    def check(self, summary):
        self.path.write_text(json.dumps(summary))
        return MODULE.checked_summary(self.path, self.member, self.protocol)

    def test_valid_result_includes_half_credit_for_draws(self):
        self.assertEqual(self.check(self.summary), self.summary)

    def test_wrong_checkpoint_seed_or_budget_rejected(self):
        for key, value in [('sha256', 'other-model'), ('seed', 7), ('budget', 128)]:
            with self.subTest(key=key):
                bad = copy.deepcopy(self.summary)
                bad[key] = value
                with self.assertRaises(AssertionError):
                    self.check(bad)

    def test_both_modes_require_expected_game_count(self):
        for mode in ('argmax', 'sample'):
            with self.subTest(mode=mode):
                bad = copy.deepcopy(self.summary)
                bad['scores'][mode]['games'] = 192
                with self.assertRaises(AssertionError):
                    self.check(bad)

    def test_inconsistent_outcomes_or_scalar_rejected(self):
        for mode in ('argmax', 'sample'):
            for key, value in [('losses', 281), ('score', .99)]:
                with self.subTest(mode=mode, key=key):
                    bad = copy.deepcopy(self.summary)
                    bad['scores'][mode][key] = value
                    with self.assertRaises(AssertionError):
                        self.check(bad)

    def test_missing_mode_rejected(self):
        del self.summary['scores']['sample']
        with self.assertRaises(KeyError):
            self.check(self.summary)


if __name__ == '__main__':
    unittest.main()
