import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('mario_sweep_worker', Path(__file__).with_name('sweep_worker.py'))
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class SweepWorkerTests(unittest.TestCase):
    def test_fixed_panel_coverage(self):
        panels = worker.evaluation_panels({'sweep.eval_level_episodes': '4', 'sweep.eval_fpg_episodes': '16'})
        levels = [p for p in panels if p[0].startswith('level_')]
        self.assertEqual(len(levels), 32)
        self.assertEqual({p[2] for p in levels}, {f'level:{w}-{l}' for w in range(1,9) for l in range(1,5)})
        starts = [p[5]+i for p in panels if p[2] == 'all-stages' for i in range(p[1])]
        self.assertEqual(starts, list(range(32)))
        self.assertEqual({p[3] for p in panels if p[4] == 'fpg'}, set(worker.FPG_DEPTHS))
        self.assertEqual(len(panels), 58)

    def panels(self, progress=0, clears=0, fpg=0):
        panels = {}
        for name, episodes, selector, depth, objective, first in worker.evaluation_panels(
                {'sweep.eval_level_episodes': '4', 'sweep.eval_fpg_episodes': '16'}):
            panels[name] = {'episodes_requested': episodes,
                            'rom_successes': episodes * (clears if name.startswith('level_') else
                                                        fpg if objective == 'fpg' else 0),
                            'mean_progress_fraction': progress}
        return panels

    def test_progress_separates_zero_clear_trials(self):
        worse = worker.score_panels(self.panels(progress=.09))
        better = worker.score_panels(self.panels(progress=.24))
        self.assertAlmostEqual(worse['score'], .0225)
        self.assertAlmostEqual(better['score'], .06)
        self.assertEqual(better['legacy_clear_only_score'], 0)
        self.assertGreater(better['score'], worse['score'])

    def test_early_fpg_mastery_counts_before_depth_96(self):
        panels = self.panels()
        for depth in worker.FPG_DEPTHS:
            if depth < 96:
                panels[f'fpg_{depth}']['rom_successes'] = 16
        score = worker.score_panels(panels)
        self.assertGreater(score['score'], .15)
        self.assertEqual(score['fpg_long_clear_rate'], 0)
        self.assertEqual(score['legacy_clear_only_score'], 0)

    def test_score_is_bounded_and_retains_completion_weight(self):
        self.assertEqual(worker.score_panels(self.panels())['score'], 0)
        self.assertEqual(worker.score_panels(self.panels(progress=1, clears=1, fpg=1))['score'], 1)
        self.assertEqual(worker.score_panels(self.panels(clears=1))['score'], .5)
        for progress in (-.1, 1.1, float('nan')):
            with self.assertRaises(ValueError):
                worker.score_panels(self.panels(progress=progress))

    def test_cache_requires_a_matching_valid_protocol(self):
        settings = {'sweep.eval_seed': '137'}
        test = ('level_3_2', 4, 'level:3-2', 0, 'clear', 0)
        summary = {'passed': True, 'mismatches': 0, 'episodes_completed': 4,
                   'episodes_requested': 4, 'seed': 137, 'first_episode': 0,
                   'reference_frames': 0, 'objective': 'level_transition',
                   'max_frames': 6000, 'starting_stage': 9}
        self.assertTrue(worker.valid_panel(summary, test, settings))
        for key, value in (('mismatches', 1), ('episodes_completed', 3), ('seed', 99),
                           ('starting_stage', 8), ('max_frames', 1800)):
            self.assertFalse(worker.valid_panel(summary | {key: value}, test, settings))
        self.assertFalse(worker.valid_panel({}, test, settings))

    def test_previous_objective_does_not_prune_new_trials(self):
        settings = {'sweep.prune_min_trials': '20', 'sweep.prune_quantile': '0.1',
                    'sweep.prune_score_margin': '0.10', 'vec.total_agents': '4096'}
        with tempfile.TemporaryDirectory() as directory:
            history = Path(directory)/'rungs.jsonl'
            rows = [{'run': f'old{i}', 'rung': 500, 'agents': 4096, 'score': 1} for i in range(20)]
            history.write_text(''.join(json.dumps(row)+'\n' for row in rows))
            self.assertFalse(worker.record_rung(history, 'new', 500,
                             {'score': 0, 'checkpoint': 'model.bin'}, settings))

    def test_rung_pruning_requires_comparable_evidence_and_large_deficit(self):
        settings = {'sweep.prune_min_trials': '20', 'sweep.prune_quantile': '0.1',
                    'sweep.prune_score_margin': '0.10', 'vec.total_agents': '4096'}
        with tempfile.TemporaryDirectory() as directory:
            history = Path(directory)/'rungs.jsonl'
            result = {'score': .4, 'checkpoint': 'model.bin'}
            for i in range(20):
                self.assertFalse(worker.record_rung(history, f'run{i}', 500, result, settings))
            self.assertFalse(worker.record_rung(history, 'different_budget', 1000, {'score':0,'checkpoint':'a'}, settings))
            self.assertFalse(worker.record_rung(history, 'larger_actors', 500, {'score':0,'checkpoint':'a'},
                                               settings | {'vec.total_agents':'32768'}))
            self.assertFalse(worker.record_rung(history, 'close', 500, {'score':.32,'checkpoint':'a'}, settings))
            self.assertTrue(worker.record_rung(history, 'bad', 500, {'score':.05,'checkpoint':'a'}, settings))
            worker.record_rung(history, 'bad', 500, {'score':.05,'checkpoint':'a'}, settings)
            rows = [json.loads(line) for line in history.read_text().splitlines()]
            self.assertEqual(sum(row['run']=='bad' for row in rows), 1)

    def test_recovery_and_evaluated_checkpoints_survive_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoints = [root/f'{i:016d}.bin' for i in range(5)]
            for checkpoint in checkpoints:
                for suffix in ('', '.curriculum', '.curriculum.json', '.practice'):
                    Path(str(checkpoint)+suffix).touch()
            worker.prune_checkpoints(root, {checkpoints[1]})
            self.assertEqual(set(root.glob('*.bin')), {checkpoints[1], checkpoints[3], checkpoints[4]})
            self.assertFalse(Path(str(checkpoints[0])+'.curriculum').exists())
            self.assertFalse(Path(str(checkpoints[0])+'.curriculum.json').exists())


if __name__ == '__main__':
    unittest.main()
