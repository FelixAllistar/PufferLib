import copy
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'ocean/pokemon'))
import league


class LeagueTests(unittest.TestCase):
    def test_solver_rps(self):
        result = league.metagame.solve([[0,-1,1],[1,0,-1],[-1,1,0]], 20000, 0)
        self.assertLess(result['empirical_exploitability'], .03)
        self.assertAlmostEqual(sum(result['weights']), 1)
        self.assertTrue(all(abs(p-1/3) < .03 for p in result['weights']))

    def test_solver_dominance(self):
        result = league.metagame.solve([[0,1],[-1,0]], 5000, .1)
        self.assertGreater(result['weights'][0], .94)
        self.assertGreaterEqual(result['weights'][1], .05)
        with self.assertRaises(ValueError): league.metagame.solve([[0,1],[1,0]])

    def test_solver_flat_payoffs(self):
        result = league.metagame.solve([[0]*3 for _ in range(3)], 300, 0)
        self.assertTrue(all(abs(p-1/3) < 1e-9 for p in result['weights']))

    def test_config_and_commands(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        team = c['resolved_teams']['starmie_rhydon']
        self.assertEqual(team, [421,445,514,392,367,389])
        self.assertEqual(c['resolved_teams']['alakazam_zapdos'], [231,445,513,397,368,526])
        learner = {'path':'learner.bin','team':team,'hidden':128,'layers':2}
        opponent = {'path':'enemy.bin','team':None,'hidden':128,'layers':2}
        cmd = league.train_command(c, Path('/tmp/pokemon-test'), 'test', learner, opponent, 42)
        self.assertIn('env.learner_team=421,445,514,392,367,389',cmd)
        self.assertIn('env.opponent_team=None',cmd)
        self.assertIn('selfplay.opponent_pool=enemy.bin',cmd)
        self.assertIn('selfplay.opponent_pool_prob=1',cmd)
        self.assertIn('vec.frozen_bank_pct=1',cmd)
        self.assertIn('vec.seat_balance=0',cmd)
        self.assertIn('train.epoch_sampling=1',cmd)
        self.assertIn('env.team_log_interval=0',cmd)

    def test_invalid_teams(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        catalog = league.read(ROOT/'ocean/pokemon/data/catalog.json')
        team = copy.deepcopy(c['teams']['starmie_rhydon'])
        team[0]['moves'][0] = 'Fissure'
        with self.assertRaises(ValueError): league.resolve_team(team, catalog)
        with self.assertRaises(ValueError): league.resolve_team([team[1]]*6, catalog)

    def test_eval_seat_assignment(self):
        team = '422,445,515,397,366,389'
        for side in ('a','b'):
            result = subprocess.check_output(['./pokemon','eval','random','random','--games=4','--profile-json',
                f'--team-{side}={team}', 'env.max_updates=1'], text=True, cwd=ROOT)
            rows = [json.loads(line[11:]) for line in result.splitlines() if line.startswith('PK_PROFILE ')]
            self.assertEqual(len(rows), 4)
            if side == 'a':
                for r in rows:
                    self.assertEqual(r['descriptors']['leads'][120], 1)  # Starmie #121
                    self.assertEqual(r['descriptors']['species'][127], 1)  # Tauros #128
            else:
                self.assertTrue(any(r['descriptors']['leads'][120] != 1 for r in rows))

    def test_immutable_snapshot_and_identity(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            source = d/'seed'
            source.mkdir()
            # Finite zero-weight network: no dependency on user's checkpoints.
            h,layers = 8,1
            size = (640*h+161*h+3*h*h)*4
            (source/'seed.bin').write_bytes(bytes(size))
            (source/'config.ini').write_text(f'[env]\nabi_version=2\ncatalog_sha={c["catalog_sha"]}\n[policy]\nhidden_size=8\nnum_layers=1\n')
            league.initialize(c, d, source/'seed.bin')
            state = league.read(d/'state.json')
            league.check_state(c, state)
            state['policies']['historical_extra'] = dict(state['policies']['master_initial'])
            c['games'], c['max_updates'] = 2,1
            with mock.patch.object(league,'pair_profiles',wraps=league.pair_profiles) as pairs:
                report = league.evaluate(c, d, state)
                self.assertEqual(pairs.call_count,55)
            self.assertEqual(len(report['weights']), 11)
            native = league.configparser.ConfigParser(interpolation=None)
            native.read(d/'native.ini')
            self.assertEqual(native.getint('native','banks'),32)
            self.assertEqual({native.get(f'bank.{i}','name') for i in range(32)},set(state['current']))
            for i in range(32):
                name=native.get(f'bank.{i}','name')
                p=state['policies'][state['current'][name]]
                self.assertEqual(native.get(f'bank.{i}','path'),p['path'])
                self.assertEqual(native.get(f'bank.{i}','team'),league.team_arg(p['team']))
            self.assertEqual(set(report['pool']), {m['name'] for m in c['members']})
            self.assertNotIn('selection', report)
            for i,row in enumerate(report['matrix']):
                self.assertEqual(row[i],0)
                for j,value in enumerate(row): self.assertEqual(value,-report['matrix'][j][i])
            profiles = league.read(Path(state['evaluation']).with_name('profiles.json'))
            self.assertEqual(profiles['version'],2)
            for record in profiles['records']:
                self.assertNotIn(record['id'],record['payoffs'])
                self.assertEqual(len(record['payoffs']),10)
            original = league.baseline_check(c,d,state)
            panel = league.read(d/'baseline_panel.json')
            state['current']['master'] = 'historical_extra'
            again = league.baseline_check(c,d,state)
            self.assertEqual(original['panel_id'],again['panel_id'])
            self.assertEqual(panel,league.read(d/'baseline_panel.json'))
            self.assertEqual(set(state['current']),set(report['pool']))
            state['current']['master'] = 'master_initial'
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured): league.train_round(c,d,state,False)
            for line in captured.getvalue().splitlines():
                if ': chunk ' in line:
                    member = line.split(':')[0]
                    opponent = line.split(' vs ')[1]
                    self.assertNotEqual(member, opponent)
                    self.assertIn(opponent, state['current'])
            self.assertAlmostEqual(sum(report['weights']), 1)
            self.assertTrue((d/'state.json').exists())
            learner = state['policies']['starmie_rhydon_initial']
            text = Path(learner['path']).with_name('config.ini').read_text()
            self.assertIn('learner_team = 421,445,514,392,367,389', text)
            with self.assertRaises(ValueError): league.initialize(c,d,source/'seed.bin')
            changed = copy.deepcopy(c)
            changed['resolved_teams']['starmie_rhydon'].reverse()
            with self.assertRaises(ValueError): league.check_state(changed,state)

    @unittest.skipUnless(os.environ.get('POKEMON_LEAGUE_SMOKE_CHECKPOINT'), 'optional native GPU smoke')
    def test_native_round(self):
        c = league.read(ROOT/'ocean/pokemon/league.json')
        c['members'] = [m for m in c['members'] if m['name'] in ('master','starmie_rhydon','articuno_core')]
        with tempfile.TemporaryDirectory(prefix='league-smoke-', dir=ROOT/'build/pokemon') as tmp:
            c.update(directory=str(Path(tmp)/'league'), games=2, max_updates=16,
                     chunk_steps=32768, chunks_per_member=1)
            c['vec_overrides'] = {'vec.total_agents':128, 'vec.num_threads':1, 'vec.num_buffers':1}
            c['train_overrides']['train.minibatch_size'] = 8192
            config = Path(tmp)/'config.json'
            config.write_text(json.dumps(c))
            prefix = [sys.executable,'ocean/pokemon/league.py','--config',str(config)]
            for args in (['init','--fresh'], ['run','--rounds','1']):
                result = subprocess.run(prefix+args,cwd=ROOT,text=True,capture_output=True,timeout=180)
                self.assertEqual(result.returncode,0,result.stdout[-5000:]+result.stderr[-5000:])
            state = league.read(Path(c['directory'])/'state.json')
            self.assertEqual(state['round'],1)
            self.assertEqual(state['initialization'],'fresh')
            self.assertEqual(len({p['sha256'] for name,p in state['policies'].items() if name.endswith('_initial')}),3)
            self.assertEqual(set(league.read(state['evaluation'])['pool']), set(state['current']))
            self.assertIsNotNone(state['evaluation'])
            self.assertFalse((Path(c['directory'])/'pending.json').exists())
            for name,identity in state['current'].items():
                self.assertNotEqual(identity, name+'_initial')
                self.assertEqual(state['policies'][identity]['team'],state['assignments'][name])

    def test_group_budgets(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        pool = [m['name'] for m in c['members']]
        groups = {m['name']:m['group'] for m in c['members']}
        # Even a solver that assigns everything to a niche cannot erase OU.
        raw = [float(n=='articuno_core') for n in pool]
        for excluded in (None,'master','articuno_core'):
            weights = league.opponent_weights(c,pool,raw,excluded)
            self.assertAlmostEqual(sum(weights),1)
            self.assertAlmostEqual(sum(w for n,w in zip(pool,weights) if groups[n]=='ou'),.8)
            if excluded: self.assertEqual(weights[pool.index(excluded)],0)

    def test_required_config_and_eval(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        spec = league.team_arg(c['resolved_teams']['articuno_core'])
        self.assertEqual(spec,'required:445,512,392,522')
        result = subprocess.check_output(['./pokemon','eval','random','random','--games=32','--profile-json',
            f'--team-a={spec}',f'--team-b={spec}','env.max_updates=1'],text=True,cwd=ROOT)
        rows = [json.loads(line[11:]) for line in result.splitlines() if line.startswith('PK_PROFILE ')]
        self.assertEqual(len(rows),32)
        leads = set()
        for row in rows:
            for species in (128,143,113,144): self.assertEqual(row['descriptors']['species'][species-1],1)
            leads.add(tuple(row['descriptors']['leads']))
        self.assertGreater(len(leads),1)

    def test_schedule(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        self.assertEqual([league.evaluation_kind(c,n) for n in range(7)],
                         ['full','quick','quick','quick','quick','full','quick'])
        self.assertEqual(league.evaluation_kind(c,7,final=True),'full')

    def test_run_schedule_boundaries(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        for rounds in (5,6):
            state = {'round':0,'evaluation':None}
            evaluations, baselines = [],[]
            def trained(c,d,state,execute):
                self.assertTrue(execute)
                return {'round':state['round']+1,'evaluation':None}
            def evaluated(c,d,state,kind): evaluations.append((state['round'],kind))
            def baseline(c,d,state): baselines.append(state['round'])
            with mock.patch.object(league,'train_round',side_effect=trained), mock.patch.object(league,'evaluate',side_effect=evaluated), mock.patch.object(league,'baseline_check',side_effect=baseline):
                result = league.run_rounds(c,Path('/tmp/not-written'),state,rounds)
            self.assertEqual(result['round'],rounds)
            self.assertEqual(baselines,[0,rounds])
            self.assertEqual(evaluations,[(0,'full'),(1,'quick'),(2,'quick'),(3,'quick'),(4,'quick'),(5,'full')] + ([(6,'full')] if rounds==6 else []))

    def test_both_profiles_same_games(self):
        c = league.load_config(ROOT/'ocean/pokemon/league.json')
        c['max_updates'] = 32
        a = {'path':'random','team':[421,445,514,392,367,389]}
        b = {'path':'random','team':[231,445,513,397,368,526]}
        rows_a,rows_b = league.pair_profiles(c,a,b,8,42)
        self.assertEqual(len(rows_a),8)
        for x,y in zip(rows_a,rows_b):
            self.assertEqual(x['score']+y['score'],1)
            self.assertEqual(x['descriptors']['leads'][120],1)
            self.assertEqual(y['descriptors']['leads'][64],1)
            self.assertEqual(x['descriptors']['duration'],y['descriptors']['duration'])
        # Old A-only and new two-sided logging must not change any actions/RNG.
        output = subprocess.check_output([c['eval_binary'],'eval','random','random','--profile-json',
            '--team-a='+league.team_arg(a['team']),'--team-b='+league.team_arg(b['team']),
            '--games=8','--seed=42','env.max_updates=32'],cwd=ROOT,text=True)
        plain = [json.loads(line[11:]) for line in output.splitlines() if line.startswith('PK_PROFILE ')]
        self.assertEqual(rows_a,plain)


if __name__ == '__main__': unittest.main()
