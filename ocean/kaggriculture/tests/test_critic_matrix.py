import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from run_critic_matrix import critic_gate, training_args


class MatrixTests(unittest.TestCase):
    def test_constant_or_nonfinite_critic_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            p = pathlib.Path(directory) / 'fit.log'
            for numbers in ('51 51 0', 'nan 51 nan', '52 51 0.5', '30 51 -0.1'):
                a, b, c = numbers.split()
                p.write_text(f'CRITIC_GATE val_rmse={a} constant_train_mean_rmse={b} val_ev={c}\n')
                with self.assertRaises(ValueError): critic_gate(p)
            p.write_text('CRITIC_GATE val_rmse=30 constant_train_mean_rmse=51 val_ev=0.6\n')
            self.assertEqual(critic_gate(p)['val_rmse'], 30)

    def test_branch_overrides_are_explicit_and_matched(self):
        with tempfile.TemporaryDirectory() as directory:
            p = pathlib.Path(directory) / 'profile.ini'
            p.write_text('[base]\nload_model_path=old.bin\n[train]\nanneal_lr=1\n')
            def fields(emag):
                return dict(x.split('=', 1) for x in training_args(
                    p, 'initial.bin', 'bc_reference.bin', 'opponent.bin', 'run', emag, 20643840)[3:])
            a, b = fields(False), fields(True)
            changed = {k for k in a if a[k] != b[k]}
            self.assertEqual(changed, {'train.emag_kl_coef', 'selfplay.magnet_path'})
            self.assertEqual(a['base.load_model_path'], 'initial.bin')
            self.assertEqual(a['env.reset_state_prob'], '0.8')
            self.assertEqual(a['train.anneal_lr'], '0')
            self.assertEqual(b['selfplay.magnet_path'], 'bc_reference.bin')
            self.assertEqual(int(a['train.total_timesteps']), 14 * 2048 * 720)


if __name__ == '__main__':
    unittest.main()
