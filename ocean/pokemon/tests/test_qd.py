"""CPU/math/protocol tests, not evidence of Pokemon strength or GPU correctness."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from types import SimpleNamespace
import numpy as np

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from cma_qd import CMAES, Archive, personality, behavior_distance
from qd import aggregate, evaluate, execute, report


def row(ids=(1,2,3,4,5,6), score=.5, sets=None):
    return dict(qd_version=1, species_ids=list(map(int,ids)), set_ids=list(map(int,sets or ids)),
                personality=[0,0,0,0,1], score=score)


def rec(name, ids=(1,2,3,4,5,6), score=.5, noise=0):
    return dict(id=name, quality=score, behavior=aggregate([row(ids)]), noise=noise)


class TestCMA(unittest.TestCase):
    def test_contract(self):
        es=CMAES(); es.ask()
        with self.assertRaises(RuntimeError): es.ask()
        with self.assertRaises(ValueError): es.tell([float('nan')]*8)
        es.tell(list(range(8)))
        with self.assertRaises(ValueError): es.tell(list(range(8)))

    def test_sphere(self):
        es=CMAES(seed=11); target=np.array([1,2,-1,0,.5])
        for _ in range(130): es.tell(-np.sum((es.ask()-target)**2, axis=1))
        self.assertLess(np.linalg.norm(es.mean-target), 1e-3)
        self.assertTrue(np.all(np.linalg.eigvalsh(es.C)>0))

    def test_rotated_ellipsoid(self):
        Q,_=np.linalg.qr(np.random.default_rng(3).normal(size=(5,5)))
        es=CMAES(seed=4); es.mean=np.ones(5)*2
        def score(x): return -np.sum((x@Q)**2*np.array([1,2,10,30,100]), axis=1)
        initial=-score(es.mean[None,:])[0]
        for _ in range(180): es.tell(score(es.ask()))
        self.assertLess(-score(es.mean[None,:])[0], initial*1e-5)
        self.assertGreater(np.linalg.norm(es.C-np.diag(np.diag(es.C))), 1e-9)

    def test_resume(self):
        es=CMAES(seed=7); es.tell(-np.sum(es.ask()**2, axis=1))
        other=CMAES.restore(json.loads(json.dumps(es.state())))
        np.testing.assert_array_equal(es.ask(), other.ask())

    def test_weight_bound(self):
        for x in np.random.default_rng(0).normal(size=(100,5))*100:
            self.assertLessEqual(sum(map(abs,personality(x))), 1.0000001)
        self.assertEqual(personality(np.zeros(5)), [0]*5)


class TestArchive(unittest.TestCase):
    def test_permutations(self):
        a=aggregate([row()]); b=aggregate([row((1,6,5,4,3,2))])
        self.assertEqual(behavior_distance(a,b), 0)
        self.assertGreater(behavior_distance(a,aggregate([row((2,1,3,4,5,6))])), 0)

    def test_joint_not_marginal(self):
        a=aggregate([row((1,2,3,4,5,6)),row((7,8,9,10,11,12))])
        b=aggregate([row((1,2,3,7,8,9)),row((4,5,6,10,11,12))])
        self.assertEqual(a['species'],b['species'])
        self.assertGreater(behavior_distance(a,b),.1)

    def test_novel_weak_local_strong(self):
        arc=Archive(); arc.add_batch([rec('a',score=.8)])
        r=arc.add_batch([rec('b',(7,8,9,10,11,12),.2),rec('c',score=.9)])
        self.assertEqual(r['b'][0],2); self.assertEqual(r['c'][0],1)
        self.assertEqual({r['id'] for r in arc.records},{'b','c'})

    def test_batch_order(self):
        rs=[rec('b',score=.6),rec('a',score=.7),rec('z',(7,8,9,10,11,12),.1)]
        a,b=Archive(),Archive()
        self.assertEqual(a.add_batch(rs),b.add_batch(rs[::-1]))
        self.assertEqual(a.records,b.records)

    def test_noise_guard(self):
        arc=Archive(radius=.01); arc.add_batch([rec('a',noise=1)])
        self.assertEqual(arc.add_batch([rec('b',(7,8,9,10,11,12),.4,1)])['b'][0],0)
        self.assertEqual(len(arc.records),1)

    def test_random_teams_not_maximal_distance(self):
        rng=np.random.default_rng(8)
        rows=[row(tuple(rng.choice(np.arange(1,150),6,replace=False))) for _ in range(512)]
        self.assertLess(behavior_distance(aggregate(rows[:256]),aggregate(rows[256:])),.1)

    def test_capacity_nan(self):
        a=Archive(capacity=2); a.add_batch([rec('a'),rec('b',(7,8,9,10,11,12))])
        self.assertEqual(a.add_batch([rec('c',(20,21,22,23,24,25))])['c'][0],0)
        with self.assertRaises(ValueError): a.add_batch([rec('nan',score=float('nan'))])

    def test_invalid_profile(self):
        with self.assertRaises(ValueError): aggregate([dict(score=1)])
        r=row(); r['personality'][0]=float('nan')
        with self.assertRaises(ValueError): aggregate([r])


class TestContracts(unittest.TestCase):
    def test_c_math(self):
        with tempfile.TemporaryDirectory() as d:
            binary=Path(d)/'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(HERE/'tests/test_personality.c'),'-lm','-o',str(binary)],check=True)
            subprocess.run([binary],check=True)

    def test_child_failure_not_fitness(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(subprocess.CalledProcessError):
                execute([sys.executable,'-S','-c','raise SystemExit(9)'],Path(d)/'log')

    def test_evaluator_protocol(self):
        with tempfile.TemporaryDirectory() as d:
            script=Path(d)/'fake_eval'
            script.write_text('#!'+sys.executable+' -S\nimport json\nfor i in range(8): print("PK_PROFILE "+json.dumps('+repr(row())+'))\n')
            script.chmod(0o755)
            r=evaluate(str(script),'unused',[{'path':'unused'}],8,42,512,Path(d)/'eval')
            self.assertEqual(r['quality'],.5); self.assertEqual(r['noise'],0)
            script.write_text('#!'+sys.executable+' -S\nprint("missing profiles")\n')
            with self.assertRaises(ValueError): evaluate(str(script),'unused',[{'path':'unused'}],8,42,512,Path(d)/'bad')

    def test_posthoc_report(self):
        with tempfile.TemporaryDirectory() as d:
            old=Path.cwd()
            try:
                os.chdir(d); p=Path('ocean/pokemon'); p.mkdir(parents=True)
                labels=['']+[f'Species{i}_OU' for i in range(1,150)]
                labels[5]='Chansey_OU'; labels[12]='Tauros_OU'; labels[37]='Snorlax_OU'
                (p/'species_labels.h').write_text('static const char* const pk_species_labels[150] = {'+','.join(json.dumps(x) for x in labels)+'};')
                Path('eval').mkdir(); Path('eval/panel_000.log').write_text('PK_PROFILE '+json.dumps(row((5,12,37,1,2,3)))+'\n')
                out=io.StringIO()
                with redirect_stdout(out): report(SimpleNamespace(directory='eval',core='Chansey,Tauros,Snorlax'))
                self.assertEqual(json.loads(out.getvalue())['core_frequency'],1)
            finally: os.chdir(old)


if __name__=='__main__': unittest.main()
