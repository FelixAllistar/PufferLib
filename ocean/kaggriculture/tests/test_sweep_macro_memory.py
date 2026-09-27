import importlib.util
from pathlib import Path
import unittest
import json
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('memory_sweep',
    Path(__file__).resolve().parents[1] / 'sweep_macro_memory.py')
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)


class MemorySweepTests(unittest.TestCase):
    def test_ini_quotes_are_removed_for_subprocess_argv(self):
        self.assertEqual(sweep.cli_value("'None'"), 'None')
        self.assertEqual(sweep.cli_value('"some/path"'), 'some/path')
        self.assertEqual(sweep.cli_value('.004'), '.004')
        self.assertEqual(sweep.cli_value("don't"), "don't")
    def test_shapes_and_memory_budget(self):
        trials = sweep.initial_trials()
        self.assertEqual(len(trials), 14)
        self.assertEqual({(x['agents'], x['horizon']) for x in trials},
            {(4096, 256), (8192, 128), (4096, 128), (8192, 64), (16384, 32), (16384, 64)})
        for x in trials + sweep.refinements(trials):
            self.assertLessEqual(x['agents'] * x['horizon'], 1048576)
            self.assertEqual((x['agents']//4*x['horizon']) % x['minibatch'], 0)

    def test_refinement_deduplication_and_seeds(self):
        trial = sweep.initial_trials()[0]
        a = sweep.refinements([trial])
        self.assertEqual(a, sweep.refinements([trial, trial]))
        self.assertTrue(all(x['seed'] == trial['seed'] for x in a))
        self.assertEqual({x['lr'] for x in a}, {.002, .004, .006})

    def test_amend_only_before_start_and_oom_classification(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            (out / 'plan.json').write_text(json.dumps(dict(steps=100000000,
                trials=sweep.initial_trials()[:12])))
            sweep.amend_prestart(out, 300000000)
            sweep.amend_prestart(out, 300000000)
            plan = json.loads((out / 'plan.json').read_text())
            self.assertEqual(plan['steps'], 300000000)
            self.assertEqual(len(plan['trials']), 14)
            (out / 'baseline_eval.log').touch()
            with self.assertRaises(RuntimeError):
                sweep.amend_prestart(out, 1)
        self.assertTrue(sweep.is_oom('CUDA error: out of memory'))
        self.assertTrue(sweep.is_oom('cudaErrorMemoryAllocation'))
        self.assertFalse(sweep.is_oom('illegal memory access'))

    def test_orchestration_resume_and_eval_isolation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = root / 'audit'
            out.mkdir()
            (out / 'plan.json').write_text(json.dumps(dict(base={'vec.frozen_bank_pct': '.75',
                'selfplay.opponent_pool_weights': "'None'"},
                steps=100000000, games=64, trials=[sweep.initial_trials()[0]])))
            (out / 'opponents.tsv').write_text('id\tpolicy\tweight\tcheckpoint\n0\ta\t1\ta.bin\n1\tb\t1\tb.bin\n')
            calls = []
            def fake_execute(cmd, log):
                calls.append(cmd)
                v = dict(arg.split('=', 1) for arg in cmd[3:])
                self.assertEqual(v['selfplay.opponent_pool_weights'], 'None')
                if cmd[1] == 'train':
                    folder = root / 'checkpoints/kaggriculture' / v['base.run_id']
                    folder.mkdir(parents=True)
                    (folder / '0000000099614720.bin').write_bytes(b'fake')
                else:
                    self.assertTrue(v['base.run_id'].endswith('_eval'))
                    candidates = Path(v['league.candidate_manifest']).read_text().splitlines()[1:]
                    Path(v['league.output']).write_text(''.join(
                        f'{i}\t{j}\t0.5\t0\t70000\t69000\t64\n'
                        for i in range(len(candidates)) for j in range(2)))
                log.write_text('test subprocess\n')
                return 0
            with patch.object(sweep, 'ROOT', root), patch.object(sweep, 'execute', fake_execute):
                sweep.run(out)
                self.assertTrue((out / 'baseline.json').exists())
                self.assertGreater(len(list(out.glob('trial_*.json'))), 1)
                count = len(calls)
                sweep.run(out)
                self.assertEqual(len(calls), count)


if __name__ == '__main__':
    unittest.main()
