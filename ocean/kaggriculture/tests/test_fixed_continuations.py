import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('fixed_continuations',
    Path(__file__).resolve().parents[1] / 'compare_fixed_continuations.py')
study = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(study)


class FixedContinuationTests(unittest.TestCase):
    def base(self):
        return {'train.learning_rate': '0.000958086632', 'train.min_lr_ratio': '0.1',
            'train.total_timesteps': '100000000', 'vec.total_agents': '4096',
            'train.horizon': '256', 'train.anneal_lr': '1', 'train.ent_coef': '0.000328765134',
            'train.emag_kl_coef': '0.00962038059', 'train.emag_tau': '0.00503575709',
            'base.load_model_path': '/fixed/parent.bin', 'base.checkpoint_dir': 'checkpoints',
            'selfplay.magnet_path': '/fixed/parent.bin.emag'}

    def test_adjacent_contrasts_and_equal_mean_lr(self):
        base = self.base()
        variants = study.variants(base)
        configs = [dict(base, **v) for _,v in variants]
        def changed(a,b):
            return {k for k in a if a[k] != b[k]}
        self.assertEqual(changed(configs[0],configs[1]), {'train.emag_kl_coef'})
        self.assertEqual(changed(configs[1],configs[2]), {'train.ent_coef'})
        self.assertEqual(changed(configs[2],configs[3]), {'train.anneal_lr','train.learning_rate'})
        lr = float(base['train.learning_rate'])
        expected = sum(lr * (.1 + .9*.5*(1+math.cos(math.pi*t/95))) for t in range(95))/95
        self.assertAlmostEqual(float(configs[3]['train.learning_rate']),expected,places=14)
        self.assertEqual(base, self.base())

    def test_every_command_uses_same_parent_and_unique_id(self):
        cmds = [study.command(self.base(),name,overrides) for name,overrides in study.variants(self.base())]
        values = [dict(arg.split('=',1) for arg in cmd[3:]) for cmd in cmds]
        self.assertEqual({v['base.load_model_path'] for v in values}, {'/fixed/parent.bin'})
        self.assertEqual(len({v['base.run_id'] for v in values}), 4)
        self.assertTrue(all(v['base.result_fd']=='0' for v in values))
        with self.assertRaises(ValueError):
            study.command(dict(self.base(), **{'base.load_model_path':'latest'}),'bad',{})

    def test_changed_parent_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'parent.bin'; path.write_bytes(b'original')
            plan = {'hashes':{str(path):study.digest(path)}}
            study.verify(plan)
            path.write_bytes(b'changed')
            with self.assertRaises(ValueError): study.verify(plan)

    def test_dry_run_never_executes(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            plan = {'base':self.base(), 'hashes':{},
                    'trials':[{'label':n,'overrides':v} for n,v in study.variants(self.base())]}
            (out/'plan.json').write_text(json.dumps(plan))
            with patch.object(study,'execute',side_effect=AssertionError('must not train')):
                study.run(out,dry_run=True)


if __name__ == '__main__': unittest.main()
