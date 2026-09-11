import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from expansion_experiment import frozen_values, trial_grid, argv
from experiment_behavior import summary


class ExperimentTests(unittest.TestCase):
    def test_grid(self):
        grid = trial_grid([42])
        self.assertEqual(len(grid), 6)
        self.assertEqual({(p['deadline'], p['expansion']) for p in grid},
                         {(d, s) for d in (240, 300, 360) for s in (3, 10)})

    def test_fresh_and_optimizer_preserved(self):
        base = {'base.load_model_path': 'champ.bin', 'train.learning_rate': '.0004',
                'train.emag_kl_coef': '.01', 'env.reward_progress_scale': '10',
                'env.reward_potential_scale': '1', 'env.reward_money_scale': '1',
                'env.reward_expansion_land_target': '3'}
        values = frozen_values(base, Path('/tmp/experiment'))
        self.assertEqual(values['base.load_model_path'], 'None')
        self.assertEqual(values['train.learning_rate'], '.0004')
        self.assertEqual(values['train.emag_kl_coef'], '.01')
        self.assertEqual(values['env.reward_progress_scale'], '0')
        self.assertEqual(values['env.reward_potential_scale'], '0')
        self.assertEqual(values['env.reward_progress_terminal_money_scale'], '4')
        self.assertEqual(values['env.reward_expansion_land_target'], '3')
        self.assertEqual(base['base.load_model_path'], 'champ.bin')
        self.assertIn('base.load_model_path=None', argv('train', values))

    def test_never_events_not_reported_as_early(self):
        from experiment_behavior import FIELDS
        episode = dict(first_cow_turn=None, first_milk_turn=None, milk_by_300=0,
                       productive_extra_tile_turns=0, final=dict.fromkeys(FIELDS, 0))
        result = summary([episode])
        self.assertEqual(result['first_cow_turn_never_fraction'], 1)
        self.assertIsNone(result['first_cow_turn_conditional_mean'])
        self.assertEqual(result['cow_by_150_fraction'], 0)


if __name__ == '__main__':
    unittest.main()
