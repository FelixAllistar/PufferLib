import pathlib
import sys
import unittest
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from audit_bc_openings import first_commands
from run_expanded_bc import frozen_args, run


class OpeningTests(unittest.TestCase):
    def test_first_not_last_and_separate_commands(self):
        result = first_commands([
            {'market': [['HIRE'], ['BUY_ANIMAL', 'COW', 1]]},
            {'farmer': ['BUILD_PASTURE'], 'market': [['BUY_ANIMAL', 'COW', 2]]},
            {'hands': [['PLACE', 'COW', 1]], 'market': [['BUY_LAND']]},
        ])
        self.assertEqual(result, dict(hire=0, buy_cow=0, build_pasture=1, place_cow=2, buy_land=2))

    def test_absence_not_zero(self):
        self.assertEqual(first_commands([{}, {'farmer': ['PASS']}]), {})

    def test_frozen_ppo_args_exclude_bc_and_sweep(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'profile.ini'
            path.write_text('[env]\nreset_state_prob=0.8\n[policy]\nhidden_size=256\n'
                            '[bc]\nload_model_path=None\n[sweep]\nmax_runs=500\n')
            self.assertEqual(frozen_args(path), ['policy.hidden_size=256', 'env.reset_state_prob=0.8'])

    def test_failed_command_stops_pipeline_and_preserves_log(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'failure.log'
            with self.assertRaises(RuntimeError):
                run([sys.executable, '-c', 'print("expected failure"); raise SystemExit(2)'], path)
            self.assertEqual(path.read_text().strip(), 'expected failure')


if __name__ == '__main__':
    unittest.main()
