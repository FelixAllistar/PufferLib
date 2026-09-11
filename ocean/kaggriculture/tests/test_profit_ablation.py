import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from profit_ablation import variants, trial_values

class ProfitTests(unittest.TestCase):
    def test_factorial(self):
        self.assertEqual({(r['expansion'],r['win']) for r in variants()}, {(0,0),(0,1),(3,0),(3,1)})
        self.assertEqual(variants()[0]['expansion'],3)

    def test_same_parent_optimizer_and_cash(self):
        plan=dict(parent='/parent.bin',steps=300000000,values={
            'train.learning_rate':'.0004','train.emag_kl_coef':'.01',
            'env.reward_progress_terminal_money_scale':'4','base.load_model_path':'None'})
        for trial in variants():
            v=trial_values(plan,trial,Path('/out'),'trial')
            self.assertEqual(v['base.load_model_path'],'/parent.bin')
            self.assertEqual(v['train.learning_rate'],'.0004')
            self.assertEqual(v['train.emag_kl_coef'],'.01')
            self.assertEqual(v['env.reward_progress_terminal_money_scale'],'4')
            self.assertEqual(v['env.reward_progress_win_scale'],str(trial['win']))
            self.assertEqual(v['env.reward_expansion_scale'],str(trial['expansion']))
        self.assertEqual(plan['values']['base.load_model_path'],'None')

    def test_phase_arm_does_not_change_cash_or_lr(self):
        plan=dict(parent='/parent.bin',steps=300000000,values={'train.learning_rate':'.0004',
                  'env.reward_progress_terminal_money_scale':'4'})
        v=trial_values(plan,dict(seed=42,deadline=240,expansion=0,win=1,phase=2),Path('/out'),'phase')
        self.assertEqual(v['env.reward_phase_scale'],'2')
        self.assertEqual(v['env.reward_expansion_scale'],'0')
        self.assertEqual(v['train.learning_rate'],'.0004')
        self.assertEqual(v['env.reward_progress_terminal_money_scale'],'4')

if __name__=='__main__': unittest.main()
