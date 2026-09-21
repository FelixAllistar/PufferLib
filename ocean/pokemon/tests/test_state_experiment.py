import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
import numpy as np

SCRIPT = Path(__file__).resolve().parents[1] / 'state_experiment.py'
spec = importlib.util.spec_from_file_location('state_experiment', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class StateExperimentTests(unittest.TestCase):
    def source(self, directory):
        directory.mkdir()
        (directory / 'config.ini').write_text('[env]\nabi_version=2\nlearner_team=None\n'
                                            '[policy]\nhidden_size=8\nnum_layers=1\n')
        weights = np.arange(8 * 640 + 8 * 161 + 3 * 8 * 8, dtype='<f4') / 10000
        path = directory / '0001.bin'
        path.write_bytes(weights.tobytes())
        return path, weights

    def test_migration_changes_only_reserved_column_and_preserves_original(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, original = self.source(root / 'source')
            original_hash = module.digest(source)
            result = module.migrate(source, root / 'copy')
            actual = np.frombuffer(Path(result['path']).read_bytes(), dtype='<f4')
            expected = original.copy()
            expected[:8 * 640].reshape(8, 640)[:, 476] = 0
            np.testing.assert_array_equal(actual, expected)
            self.assertEqual(module.digest(source), original_hash)
            self.assertEqual(module.migrate(source, root / 'copy'), result)
            cfg = module.read_ini(root / 'copy/config.ini')
            self.assertEqual(cfg['env']['reset_state_prob'], '0')
            self.assertEqual(cfg['env']['reset_observation_version'], '1')

    def test_never_erases_learned_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, _ = self.source(root / 'source')
            cfg = source.parent / 'config.ini'
            cfg.write_text(cfg.read_text().replace('abi_version=2', 'abi_version=2\nreset_state_prob=0.5'))
            with self.assertRaisesRegex(ValueError, 'pre-state-bank'):
                module.migrate(source, root / 'copy')

    def test_paired_options_change_only_reset_probability_and_output_names(self):
        args = types.SimpleNamespace(train_steps=20_000_000)
        contract = {'parent': {'path': '/parent.bin', 'hidden_size': 128, 'num_layers': 2},
                    'pool': [{'path': '/opponent0.bin'}, {'path': '/opponent1.bin'}]}
        _, root = module.train_options(args, Path('/output'), contract, {'path': '/states.pks'}, 6101, 'root')
        _, states = module.train_options(args, Path('/output'), contract, {'path': '/states.pks'}, 6101, 'states')
        changed = {k for k in root if root[k] != states[k]}
        self.assertEqual(changed, {'base.run_id', 'base.checkpoint_dir', 'base.log_dir', 'env.reset_state_prob'})
        self.assertEqual(states['selfplay.opponent_pool_prob'], 1)
        self.assertEqual(states['selfplay.pfsp_alpha'], 0)
        self.assertEqual(states['selfplay.pfsp_uniform_mix'], 1)
        self.assertEqual(states['base.reset_every_horizon'], 0)
        self.assertEqual(states['train.emag_kl_coef'], 0)
        self.assertEqual(states['env.behavior_sleep'], 0)

    def test_failed_attempt_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'failed.log'
            with self.assertRaises(subprocess.CalledProcessError):
                module.execute([sys.executable, '-c', 'print("failure"); raise SystemExit(1)'], log)
            with self.assertRaisesRegex(ValueError, 'Existing attempt'):
                module.execute([sys.executable, '-c', 'print("overwrite")'], log)
            self.assertEqual(log.read_text().strip(), 'failure')


if __name__ == '__main__':
    unittest.main()
