"""Event-only archive/CMA contracts and an end-to-end mocked process run."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import numpy as np
HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from cma_qd import Archive, CMAES
from event_qd import event_descriptor, event_distance, event_weights, next_parent


def row(events=(0, 0), species=(1, 2, 3, 4, 5, 6), sets=(0, 1, 2, 3, 4, 5)):
    return dict(qd_version=1, behavior_version=1, score=.5, species_ids=list(species),
                set_ids=list(sets), personality=[0]*5, behavior_events=list(events),
                opponent_behavior_events=[0, 0])


MOCK = r'''
import configparser,json,pathlib,sys
args=sys.argv[1:]
if args[0]=='train':
    c=configparser.ConfigParser(interpolation=None);c.read('config/pokemon.ini')
    for arg in args[2:]:
        key,value=arg[2:].split('=',1);section,key=key.split('.',1)
        if not c.has_section(section):c.add_section(section)
        c[section][key]=value
    assert all(c['env']['personality_'+k]=='0' for k in ('paralysis','sleep','offense','defense','reserve'))
    assert c['env']['native_league']=='None' and c['selfplay']['enabled']=='1'
    assert c['selfplay']['opponent_pool']=='None'
    assert c['train']['reward_clip']=='0'
    assert sum(abs(float(c['env']['behavior_'+k])) for k in ('sleep','paralysis'))<=.500001
    print('PK_PERSONALITY_CONFIG v=1 weights=0,0,0,0,0')
    print('PK_BEHAVIOR_CONFIG v=1 weights='+','.join(c['env']['behavior_'+k] for k in ('sleep','paralysis'))+' early_turns=5 paralysis_cap=3')
    steps=int(c['train']['total_timesteps'])//524288*524288
    folder=pathlib.Path(c['base']['checkpoint_dir'])/'pokemon'/c['base']['run_id'];folder.mkdir(parents=True)
    (folder/f'{steps:016d}.bin').write_text('mock checkpoint '+c['base']['run_id'])
    with open(folder/'config.ini','w') as f:c.write(f)
else:
    assert args[0]=='eval'
    games=int(next(v.split('=')[1] for v in args if v.startswith('--games=')))
    c=configparser.ConfigParser();c.read(str(pathlib.Path(args[1]).parent/'config.ini'))
    sleep=float(c['env'].get('behavior_sleep','0'));para=float(c['env'].get('behavior_paralysis','0'))
    for i in range(games):
        print('PK_PROFILE '+json.dumps(dict(qd_version=1,behavior_version=1,score=.5,
            species_ids=[1,2,3,4,5,6],set_ids=[0,1,2,3,4,5],personality=[0]*5,
            behavior_events=[int(sleep>=0),3 if para>0 else 0],opponent_behavior_events=[0,0])))
'''


class TestEventArchive(unittest.TestCase):
    def test_weight_bounds_and_mirrored_resume(self):
        es = CMAES(dimension=2, population=4, seed=601, sigma=1)
        state = es.state()
        x = es.ask(mirrored=True)
        np.testing.assert_allclose(x[:2], -x[2:])
        np.testing.assert_array_equal(x, CMAES.restore(state).ask(mirrored=True))
        for coords in list(x)+[[0, 0], [100, -100], [-100, 0]]:
            w = event_weights(coords)
            self.assertLessEqual(sum(map(abs, w)), .500000001)
        self.assertEqual(event_weights([0, 0]), [0, 0])
        for bad in ([1], [1, 2, 3], [float('nan'), 0]):
            with self.assertRaises(ValueError):event_weights(bad)

    def test_weaker_novel_region_survives_and_local_quality_improves(self):
        control = dict(id='control', quality=.8, behavior=event_descriptor([row((1, 0))]))
        specialist = dict(id='specialist', quality=.69, behavior=event_descriptor([row((0, 3))]))
        archive = Archive(distance=event_distance)
        ranks = archive.add_batch([specialist, control])
        self.assertEqual(len(archive.records), 2)
        self.assertEqual(ranks['specialist'][0], 2)
        better = dict(specialist, id='better', quality=.74)
        self.assertEqual(archive.add_batch([better])['better'][0], 1)
        self.assertEqual({r['id'] for r in archive.records}, {'control', 'better'})
        self.assertEqual(next_parent(archive, control)['id'], 'better')

    def test_whole_teams_not_marginals_and_cap_before_mean(self):
        a = event_descriptor([row((0, 0)), row((0, 6), (7,8,9,10,11,12), (6,7,8,9,10,11))])
        b = event_descriptor([row((0, 0), (1,2,3,10,11,12), (0,1,2,9,10,11)),
                              row((0, 6), (7,8,9,4,5,6), (6,7,8,3,4,5))])
        self.assertEqual(a['species'], b['species'])
        self.assertEqual(a['events'], [0, .5])
        self.assertGreater(event_distance(a, b), .12)
        same = copy.deepcopy(a);same['style'] = [1]*5
        self.assertEqual(event_distance(a, same), 0)


class TestEventRunner(unittest.TestCase):
    def test_two_generations_baseline_holdout_resume_and_fingerprints(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp);folder = root/'ocean/pokemon';folder.mkdir(parents=True)
            for name in ('event_qd.py', 'behavior_experiment.py', 'cma_qd.py', 'qd.py', 'bridge.zig'):
                shutil.copyfile(HERE/name, folder/name)
            (folder/'data').mkdir();(folder/'data/catalog.json').write_text('{}')
            cfg = '[policy]\nhidden_size=8\nnum_layers=1\n[env]\nlearner_team=None\nbehavior_sleep=0\nbehavior_paralysis=0\n'
            (root/'config').mkdir();(root/'config/default.ini').write_text('[base]\n')
            (root/'config/pokemon.ini').write_text(cfg)
            for name in ('initial', 'opponent', 'heldout', 'warm'):
                p = root/name;p.mkdir();(p/'model.bin').write_text(name);(p/'config.ini').write_text(cfg)
            import hashlib
            def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
            opponent = root/'opponent/model.bin'
            (root/'panel.json').write_text(json.dumps(dict(opponents=[dict(path=str(opponent),
                sha256=sha(opponent), config_sha256=sha(opponent.parent/'config.ini'))])))
            for name in ('trainer', 'evaluator'):
                (root/name).write_text('#!'+sys.executable+'\n'+MOCK);(root/name).chmod(0o755)
            cmd = [sys.executable, str(folder/'event_qd.py'), '--parent', str(root/'initial/model.bin'),
                   '--panel', str(root/'panel.json'), '--out', str(root/'run'),
                   '--warmstart', str(root/'warm/model.bin'), '--holdout', str(root/'heldout/model.bin'),
                   '--trainer', str(root/'trainer'), '--evaluator', str(root/'evaluator'),
                   '--train-steps', '1048576', '--games', '2', '--generations', '2']
            first = subprocess.run(cmd, cwd=root, text=True, capture_output=True)
            self.assertEqual(first.returncode, 0, first.stdout+first.stderr)
            state = json.loads((root/'run/state.json').read_text())
            self.assertEqual((state['generation'], state['phase']), (2, 'review_ready'))
            self.assertEqual(state['es']['generation'], 2)
            jobs = list((root/'run/jobs').glob('*/trained.json'))
            self.assertEqual(len(jobs), 11)
            baseline = json.loads((root/'run/jobs/baseline/trained.json').read_text())
            self.assertEqual(baseline['actual_steps'], 10*1048576)
            self.assertEqual(baseline['parent'], str(root/'run/inputs/initial/model.bin'))
            for generation in (1, 2):
                records = [json.loads((root/f'run/jobs/g{generation:02}_{label}/trained.json').read_text())
                           for label in ('c00', 'c01', 'c02', 'c03', 'control')]
                self.assertEqual(len({r['parent'] for r in records}), 1)
                self.assertEqual(len({r['train_seed'] for r in records}), 1)
                self.assertEqual(records[-1]['weights'], [0, 0])
            review = json.loads((root/'run/review.json').read_text())
            self.assertEqual(set(review['heldout']), {'2601', '3601'})
            self.assertNotIn('baseline', {r['id'] for r in state['archive']})
            self.assertNotEqual(state['champion']['id'], 'baseline')
            before = {str(p): p.stat().st_mtime_ns for p in (root/'run/jobs').glob('*/train.log')}
            resumed = subprocess.run(cmd, cwd=root, text=True, capture_output=True)
            self.assertEqual(resumed.returncode, 0, resumed.stdout+resumed.stderr)
            self.assertEqual(before, {str(p): p.stat().st_mtime_ns for p in (root/'run/jobs').glob('*/train.log')})
            (root/'evaluator').write_text('changed')
            self.assertNotEqual(subprocess.run(cmd, cwd=root, capture_output=True).returncode, 0)
