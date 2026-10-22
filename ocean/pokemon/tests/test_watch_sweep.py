import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('watch', Path(__file__).parents[1]/'benchmarks/watch_sweep.py')
watch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(watch)


class WatchTest(unittest.TestCase):
    def test_progress(self):
        parsed = watch.parse_progress('│ Steps            232.8M      Env          14ms │\n'
                                      '│ SPS               52.8K      Copy          8ms │\n'
                                      '│ Uptime    0d 1h 13m 45s      Model      1s 123 │\n'
                                      '│ To go      0d 4h 2m 11s      Misc          1ms │')
        self.assertEqual(parsed, dict(steps=232800000, sps=52800,
                                     elapsed='0d 1h 13m 45s', remaining='0d 4h 2m 11s'))

    def test_no_score_until_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            experiment = Path(directory)
            trial = experiment/'logs/pokemon/sweep_123_0000.metamon'
            trial.mkdir(parents=True)
            (trial/'settings.json').write_text(json.dumps({'train.total_timesteps':'1000000000'}))
            view = watch.render(experiment, [['/workspace/PufferLib/puffer', 'train', '--base.run_id=sweep_123_0000']])
            self.assertIn('TRAINING', view)
            self.assertIn('Pending - no completed evaluations yet', view)
            self.assertNotIn('Best completed:   0.00%', view)
            self.assertIn('NOT RUNNING', watch.render(experiment, []))

    def test_best_is_not_last(self):
        with tempfile.TemporaryDirectory() as directory:
            experiment = Path(directory)
            for i, score in enumerate((.75, .5)):
                trial = experiment/f'logs/pokemon/sweep_123_000{i}.metamon'
                (trial/'games').mkdir(parents=True)
                (trial/'trial.json').write_text(json.dumps(dict(run=f'sweep_123_000{i}', score=score)))
                (trial/'games/summary.json').write_text(json.dumps(dict(score=score, games=24,
                          wins=int(score*24), draws=0, losses=24-int(score*24))))
            view = watch.render(experiment, [])
            self.assertIn('Best completed:   75.00% - trial 1', view)
            self.assertIn('Latest completed: 50.00% - trial 2', view)
            self.assertIn('Completed trials: 2', view)

    def test_intermediate_best_separate_from_final(self):
        with tempfile.TemporaryDirectory() as directory:
            experiment = Path(directory)
            trial = experiment/'logs/pokemon/sweep_123_0000.metamon'
            trial.mkdir(parents=True)
            (trial/'settings.json').write_text(json.dumps({'train.total_timesteps':'1000000000'}))
            (trial/'snapshot_evaluations.json').write_text(json.dumps(dict(active=None, results=[
                dict(status='complete', steps=100000000, fraction=.1, score=.75, wins=18, draws=0, losses=6, games=24),
                dict(status='complete', steps=200000000, fraction=.2, score=.5, wins=12, draws=0, losses=12, games=24)])))
            view = watch.render(experiment, [['puffer', 'train', '--base.run_id=sweep_123_0000']])
            self.assertIn('Best seen:        75.00% - trial 1 @ 10%', view)
            self.assertIn('Best completed:   Pending', view)
            self.assertIn('Latest snapshot: 50.00% - trial 1 @ 20%', view)
            self.assertIn('10%: 75% | 20%: 50%', view)


if __name__ == '__main__':
    unittest.main()
