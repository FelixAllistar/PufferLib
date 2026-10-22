"""Preflight contracts for native Metamon broad sweeps (no training required)."""
import configparser
import itertools
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[3]

def config(path=None):
    cfg=configparser.ConfigParser(interpolation=None)
    cfg.read([ROOT/'config/default.ini',path or ROOT/'config/pokemon.ini'])
    return cfg

class BroadSweepTests(unittest.TestCase):
    def test_caps_and_fresh_protocol(self):
        c=config()
        self.assertEqual(c.getint('sweep','max_runs'),500)
        self.assertEqual(c.getfloat('sweep.train.total_timesteps','max'),500_000_000)
        self.assertEqual(c.getfloat('sweep.policy.hidden_size','max'),256)
        self.assertEqual(c.getfloat('sweep.policy.num_layers','max'),3)
        self.assertEqual(c['sweep']['metric'],'metamon_score')
        self.assertIn('20261019_broad256',c['sweep']['history_path'])
        self.assertNotIn('worker_path',c['sweep'])
        self.assertEqual(c.getint('metamon','games'),2)
        self.assertEqual(c.getint('metamon','final_games'),24)
        self.assertEqual(c['base']['load_model_path'],'None')
        self.assertEqual(c.getint('train','anneal_lr'),1)
        self.assertEqual(c.getfloat('train','reward_clip'),0)
        self.assertEqual(c.getint('vec','hist_policy_hidden_size'),64)

    def test_every_initial_value_is_legal(self):
        c=config(); count=0
        for section in c:
            if not section.startswith('sweep.'):continue
            group,key=section[6:].rsplit('.',1)
            value=c.getfloat(group,key)
            lo=c.getfloat(section,'min'); hi=c.getfloat(section,'max')
            self.assertLessEqual(lo,value,section); self.assertLessEqual(value,hi,section)
            dist=c[section]['distribution']
            if dist=='int_uniform':self.assertEqual(value,int(value))
            if dist=='uniform_pow2':self.assertEqual(int(value)&(int(value)-1),0)
            step=c.getfloat(section,'step',fallback=0)
            if step:self.assertAlmostEqual((value-lo)/step,round((value-lo)/step))
            count+=lo<hi
        self.assertEqual(count,25)

    def test_all_layouts_and_discrete_mixtures_divide(self):
        c=config(); buffers=c.getint('vec','num_buffers')
        # These are the exhaustive discrete ranges declared in this experiment.
        for agents,horizon,minibatch,fraction in itertools.product(
                (4096,8192,16384),(16,32,64,128),(1024,2048,4096,8192),(0,.25,.5,.75,1)):
            with self.subTest(agents=agents,horizon=horizon,minibatch=minibatch,fraction=fraction):
                experts_per_buffer=int(fraction*agents/(2*buffers))
                learner_agents=agents-experts_per_buffer*buffers
                self.assertEqual(agents%(2*buffers),0)
                self.assertEqual(minibatch%horizon,0)
                self.assertEqual(learner_agents%(minibatch//horizon),0)
                self.assertGreaterEqual(learner_agents*horizon,minibatch)
                if fraction:self.assertGreaterEqual(experts_per_buffer,14)

if __name__=='__main__':unittest.main()
