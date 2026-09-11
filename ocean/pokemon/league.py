#!/usr/bin/env python3
"""Editable, offline PSRO-style Pokemon league. Run league.sh --help.

This is the optional roster-maintenance workflow. Ordinary master training uses
./puffer train pokemon and the exported native.ini. Members are permanent;
checkpoints are versions, not members.
"""
import argparse
import array
import configparser
import copy
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'pufferlib'))
import metagame
from profile_population import SCHEMA, aggregate, digest


def read(path):
    return json.loads(Path(path).read_text())


def write(path, data, replace=False):
    path = Path(path)
    if not replace:
        with path.open('x') as f: json.dump(data, f, indent=2, allow_nan=False)
    else:
        temp = path.with_suffix('.tmp')
        with temp.open('w') as f: json.dump(data, f, indent=2, allow_nan=False)
        os.replace(temp, path)


def resolve_team(team, catalog, presets=None):
    if team is None: return None
    partial = isinstance(team, dict)
    if partial:
        if set(team) != {'required'}: raise ValueError('partial team uses {"required": [sets]}')
        team = team['required']
    if not isinstance(team, list) or not (1 <= len(team) <= 6 if partial else len(team) == 6):
        raise ValueError('team needs six sets, or 1..6 required sets')
    result, seen = [], set()
    for mon in team:
        if isinstance(mon,str): mon = (presets or {})[mon]
        name, moves = mon['species'], mon['moves']
        if name in seen: raise ValueError(f'duplicate species: {name}')
        seen.add(name)
        matches = [s for s in catalog['sets'] if s['species'] == name and len(moves) == 4 and sorted(s['moves']) == sorted(moves)]
        if not matches: raise ValueError(f'No sourced catalog set for {name}: {moves}; use the sets command')
        result.append(matches[0]['id'])
    return {'required':result} if partial else result


def team_arg(team):
    if isinstance(team,dict): return 'required:' + ','.join(map(str,team['required']))
    return 'None' if team is None else ','.join(map(str, team))


def load_config(path):
    c = read(path)
    if c['version'] != 1: raise ValueError('unsupported league version')
    catalog = read(ROOT/'ocean/pokemon/data/catalog.json')
    c['catalog_sha'] = catalog['catalog_sha256']
    c['resolved_teams'] = {k: resolve_team(v, catalog,c.get('sets',{})) for k,v in c['teams'].items()}
    names = [m['name'] for m in c['members']]
    if len(names) < 2 or len(set(names)) != len(names) or any(not re.fullmatch(r'[a-z0-9_-]{1,32}', n) for n in names):
        raise ValueError('members need unique simple names')
    for m in c['members']:
        if m['team'] is not None and m['team'] not in c['teams']: raise ValueError('unknown team')
    groups = c.get('opponent_group_mass', {'ou':1})
    if not groups or any(not math.isfinite(v) or v <= 0 for v in groups.values()):
        raise ValueError('opponent group masses must be finite and positive')
    if any(m.get('group','ou') not in groups for m in c['members']): raise ValueError('unknown member group')
    if type(c['games']) is not int or c['games'] < 2 or c['games'] % 2: raise ValueError('games must be positive/even')
    c.setdefault('quick_games',32)
    c.setdefault('full_eval_every',5)
    c.setdefault('eval_binary','build/pokemon/pokemon-league-eval')
    c.setdefault('baseline_members',['master'])
    if type(c['quick_games']) is not int or c['quick_games'] < 2 or c['quick_games'] % 2:
        raise ValueError('quick_games must be positive/even')
    if type(c['full_eval_every']) is not int or c['full_eval_every'] < 1: raise ValueError('full_eval_every must be positive')
    if any(n not in names for n in c['baseline_members']): raise ValueError('unknown baseline member')
    for key in ('chunk_steps', 'chunks_per_member', 'max_updates', 'solver_iterations'):
        if type(c[key]) is not int or c[key] < 1: raise ValueError(f'{key} must be positive')
    if not 0 <= c['uniform_mix'] <= 1: raise ValueError('uniform_mix must be in [0,1]')
    for key in c['train_overrides']:
        if not key.startswith('train.') or key in ('train.total_timesteps', 'train.epoch_sampling', 'train.prio_alpha', 'train.prio_beta0'):
            raise ValueError(f'unsupported training override: {key}')
    for key in c['vec_overrides']:
        if key not in ('vec.total_agents', 'vec.num_threads', 'vec.num_buffers'):
            raise ValueError(f'unsupported vector override: {key}')
    defaults = configparser.ConfigParser(interpolation=None)
    defaults.read([ROOT/'config/default.ini', ROOT/'config/pokemon.ini'])
    agents = c['vec_overrides'].get('vec.total_agents', defaults.getint('vec','total_agents'))
    buffers = c['vec_overrides'].get('vec.num_buffers', defaults.getint('vec','num_buffers'))
    horizon = c['train_overrides'].get('train.horizon', defaults.getint('train','horizon'))
    minibatch = c['train_overrides'].get('train.minibatch_size', defaults.getint('train','minibatch_size'))
    if any(type(v) is not int or v < 1 for v in (agents,buffers,horizon,minibatch)) or agents % (2*buffers):
        raise ValueError('positive integer rollout settings and even agents per buffer required')
    if c['chunk_steps'] % (agents*horizon) or (agents//2*horizon) % minibatch:
        raise ValueError('chunk_steps must divide into full rollouts; learner rollout must divide into minibatches')
    return c


def snapshot(directory, identity, path, team, catalog_sha):
    """Copy, never hardlink, weights and bind their own team in saved config."""
    path = Path(path).resolve(strict=True)
    if path.suffix != '.bin': raise ValueError('use an explicit .bin checkpoint, not latest/EMA')
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(path.parent/'config.ini')
    if ini.get('env', 'catalog_sha', fallback='').strip("'\"") != catalog_sha or ini.getint('env','abi_version') != 2:
        raise ValueError(f'incompatible catalog/ABI: {path}')
    h, layers = ini.getint('policy','hidden_size'), ini.getint('policy','num_layers')
    expected = ((640*h+7)//8*8 + (161*h+7)//8*8 + layers*((3*h*h+7)//8*8))*4
    if path.stat().st_size != expected: raise ValueError('checkpoint size/architecture mismatch')
    values = array.array('f')
    with path.open('rb') as f: values.fromfile(f, expected//4)
    if not all(math.isfinite(v) for v in values): raise ValueError('non-finite checkpoint weights')
    folder = directory/'policies'/identity
    folder.mkdir(parents=True, exist_ok=False)
    target = folder/'policy.bin'
    before = digest(path)
    shutil.copyfile(path, target)
    if digest(target) != before or digest(path) != before: raise ValueError('checkpoint changed while copying')
    ini.set('env','learner_team',team_arg(team))
    ini.set('env','opponent_team','None')
    ini.set('env','team_selection','1')
    ini.set('env','team_log_interval','0')
    with (folder/'config.ini').open('x') as f: ini.write(f)
    return {'path': str(target.resolve()), 'team': team, 'sha256': digest(target),
            'config_sha256': digest(folder/'config.ini'), 'hidden': h, 'layers': layers}


def check_state(c, state):
    if state.get('version') != 2:
        raise ValueError('legacy checkpoint-population league: preserved read-only; use init --fresh in a new directory')
    if c['catalog_sha'] != state['catalog_sha']: raise ValueError('catalog changed; create a new league')
    signature = {m['name']: c['resolved_teams'].get(m['team']) for m in c['members']}
    if signature != state['assignments']:
        raise ValueError('team/member definitions changed; init a new league directory to preserve identities')
    for p in state['policies'].values():
        if digest(p['path']) != p['sha256'] or digest(Path(p['path']).parent/'config.ini') != p['config_sha256']:
            raise ValueError('immutable league checkpoint/config changed')


def evaluation_settings(c):
    return {'roster_version':2, 'pair_protocol':2, **{k:c[k] for k in ('games','quick_games','full_eval_every','baseline_members','seed','max_updates','solver_iterations','uniform_mix','descriptor_weights')},
            'groups':{m['name']:m.get('group','ou') for m in c['members']},
            'group_mass':c.get('opponent_group_mass', {'ou':1}),
            'binary_sha256':digest(ROOT/c['eval_binary'])}


def opponent_weights(c, pool, weights, exclude=None):
    """Apply explicit category budgets, then metagame weights within categories."""
    groups = {m['name']:m.get('group','ou') for m in c['members']}
    masses = c.get('opponent_group_mass', {'ou':1})
    active = {groups[n] for n in pool if n != exclude}
    total_mass = sum(masses[g] for g in active)
    result = [0.0]*len(pool)
    for group in active:
        indices = [i for i,n in enumerate(pool) if n != exclude and groups[n] == group]
        total = sum(weights[i] for i in indices)
        for i in indices:
            result[i] = masses[group]/total_mass * (weights[i]/total if total else 1/len(indices))
    return result


def sync_members(c, directory, state):
    """Add explicit config members; never replace existing teams or weights."""
    if state.get('version') != 2: raise ValueError('sync requires a named-roster state')
    wanted = {m['name']:c['resolved_teams'].get(m['team']) for m in c['members']}
    for name,team in state['assignments'].items():
        if name not in wanted or wanted[name] != team:
            raise ValueError(f'sync cannot remove or redefine existing member {name}')
    updated = copy.deepcopy(state)
    for index,m in enumerate(c['members']):
        name = m['name']
        if name in state['current']: continue
        identity = f'{name}_initial'
        path = fresh_checkpoint(c,directory,name,c['seed']+index)
        updated['policies'][identity] = snapshot(directory,identity,path,wanted[name],c['catalog_sha'])
        updated['current'][name] = identity
        updated['assignments'][name] = wanted[name]
    updated['evaluation'] = None
    check_state(c,updated)
    write(directory/'state.json',updated,replace=True)
    print(f'Roster synced: {len(updated["current"])} members; existing weights preserved. Run eval or run next.')


def export_native(c, directory, state):
    """Quantize current roster weights into immutable native inference banks."""
    check_state(c,state)
    names=sorted(state['current'])
    slots=c.get('native_bank_slots',32)
    if type(slots) is not int or not len(names)<=slots<=32:
        raise ValueError('native_bank_slots must cover every member and be <=32')
    weights=opponent_weights(c,names,[1.0]*len(names))
    if state['evaluation']:
        report=read(state['evaluation'])
        if report.get('checkpoints')==state['current'] and set(report['pool'])==set(names):
            weights=[dict(zip(report['pool'],report['weights']))[n] for n in names]
    # At least one slot per member; fill largest remaining target deficits.
    counts=[1]*len(names)
    for _ in range(slots-len(names)):
        i=max(range(len(names)),key=lambda i:weights[i]*slots-counts[i])
        counts[i]+=1
    policies=[state['policies'][state['current'][n]] for n in names]
    arch={(p['hidden'],p['layers']) for p in policies}
    if len(arch)!=1: raise ValueError('native banks currently require one common opponent architecture')
    hidden,layers=next(iter(arch))
    manifest=configparser.ConfigParser(interpolation=None)
    manifest['native']={'banks':str(slots),'hidden_size':str(hidden),'num_layers':str(layers),
                        'catalog_sha':c['catalog_sha'],'round':str(state['round'])}
    bank=0
    for name,p,n in zip(names,policies,counts):
        for _ in range(n):
            manifest[f'bank.{bank}']={'name':name,'path':p['path'],'team':team_arg(p['team'])}
            bank+=1
    tmp=directory/'native.ini.tmp'
    with tmp.open('w') as f: manifest.write(f)
    os.replace(tmp,directory/'native.ini')
    print('Native frozen roster: '+', '.join(f'{name}={n}/{slots}' for name,n in zip(names,counts)))
    print(f'Published {directory/"native.ini"}; only read at the start of ordinary training.')
    return counts


def fresh_checkpoint(c, directory, name, seed):
    # Export native random initialization using one zero-learning-rate rollout.
    # No old model, optimizer, or magnet is loaded; use a small bootstrap batch.
    run_id = f'init_{name}'
    output = directory/'initialization'/'pokemon'/run_id
    if output.exists(): raise ValueError(f'initialization output already exists: {output}')
    opts = {'base.run_id':run_id, 'base.seed':seed, 'base.load_model_path':'None',
        'base.load_enemy_model_path':'None', 'base.async':0,
        'base.checkpoint_dir':str((directory/'initialization').resolve()),
        'base.log_dir':str((directory/'logs').resolve()),
        'selfplay.enabled':0, 'selfplay.magnet_path':'None',
        'vec.total_agents':128, 'vec.num_buffers':1, 'vec.num_threads':1,
        'vec.num_frozen_banks':0, 'vec.frozen_bank_pct':0, 'vec.seat_balance':0,
        'env.native_league':'None', 'env.learner_team':'None', 'env.opponent_team':'None', 'env.team_selection':1,
        'env.seed':seed, 'env.team_log_interval':0,
        'train.total_timesteps':16384, 'train.horizon':128, 'train.minibatch_size':8192,
        'train.learning_rate':0, 'train.anneal_lr':0, 'train.emag_kl_coef':0,
        'train.epoch_sampling':1, 'train.prio_alpha':0, 'train.prio_beta0':0}
    cmd = ['./puffer','train','pokemon',*[f'{k}={v}' for k,v in opts.items()]]
    result = subprocess.run(cmd, text=True, capture_output=True)
    if result.returncode: raise ValueError(f'native initialization failed: {result.stdout[-2000:]} {result.stderr[-2000:]}')
    paths = sorted(output.glob('*.bin'))
    if not paths: raise ValueError('native initialization did not save weights')
    return paths[-1]


def initialize(c, directory, checkpoint=None):
    if (directory/'state.json').exists(): raise ValueError('league already initialized')
    state = {'version': 2, 'catalog_sha': c['catalog_sha'], 'round': 0,
             'assignments': {}, 'policies': {}, 'current': {}, 'evaluation': None,
             'initialization':'fresh' if checkpoint is None else 'checkpoint'}
    for index, m in enumerate(c['members']):
        name, team = m['name'], c['resolved_teams'].get(m['team'])
        identity = f'{name}_initial'
        source = checkpoint if checkpoint is not None else fresh_checkpoint(c, directory, name, c['seed']+index)
        state['policies'][identity] = snapshot(directory, identity, source, team, c['catalog_sha'])
        state['assignments'][name] = team
        state['current'][name] = identity
    write(directory/'state.json', state)
    print(f'Initialized {len(state["current"])} named policies in {directory} ({state["initialization"]}); teams bound.')


def pair_profiles(c, a, b, games, seed):
    cmd = [str((ROOT/c['eval_binary']).resolve()),'eval',a['path'],b['path'], '--profile-both-json',
           f'--team-a={team_arg(a["team"])}', f'--team-b={team_arg(b["team"])}',
           f'--games={games}', f'--seed={seed}', 'env.team_selection=1',
           'env.team_log_interval=0', f'env.max_updates={c["max_updates"]}']
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    sides, side = {'A':[], 'B':[]}, None
    for line in result.stdout.splitlines():
        if line.startswith('PK_SIDE '): side = line[8:]
        elif line.startswith('PK_PROFILE '):
            if side not in sides: raise ValueError('missing profile side marker')
            sides[side].append(json.loads(line[11:]))
            side = None
    if any(len(rows) != games for rows in sides.values()): raise ValueError('missing paired profiles; rebuild league evaluator')
    if any(a['score']+b['score'] != 1 for a,b in zip(sides['A'],sides['B'])):
        raise ValueError('paired results are not zero sum')
    return sides['A'],sides['B']


def evaluation_kind(c, round_number, final=False):
    return 'full' if final or round_number % c['full_eval_every'] == 0 else 'quick'


def run_rounds(c, directory, state, rounds):
    if rounds < 1: raise ValueError('rounds must be positive')
    if not state['evaluation'] or read(state['evaluation'])['settings'] != evaluation_settings(c):
        evaluate(c,directory,state,'full')
    baseline_check(c,directory,state)
    for index in range(rounds):
        state = train_round(c,directory,state,True)
        evaluate(c,directory,state,evaluation_kind(c,state['round'],final=index == rounds-1))
    baseline_check(c,directory,state)
    return state


def baseline_check(c, directory, state):
    """Fixed initial snapshots, evaluation-only. Never training opponents."""
    panel_path = directory/'baseline_panel.json'
    if panel_path.exists():
        panel = read(panel_path)
    else:
        panel = {'version':1, 'references':{}}
        for name in sorted(state['current']):
            identity = name+'_initial'
            if identity not in state['policies']: raise ValueError(f'missing initial baseline: {name}')
            panel['references'][name] = copy.deepcopy(state['policies'][identity])
        write(panel_path,panel)
    refs = panel['references']
    for ref in refs.values():
        if digest(ref['path']) != ref['sha256'] or digest(Path(ref['path']).parent/'config.ini') != ref['config_sha256']:
            raise ValueError('frozen baseline was modified')
    contract = {'panel':panel, 'seed':c['seed']+1000003, 'games':c['games'],
                'max_updates':c['max_updates'], 'binary_sha256':digest(ROOT/c['eval_binary'])}
    panel_id = hashlib.sha256(json.dumps(contract,sort_keys=True).encode()).hexdigest()
    folder = directory/'baselines'
    folder.mkdir(exist_ok=True)
    report = {'round':state['round'],'panel_id':panel_id,'contract':contract,'members':{}}
    print(f'Fixed-baseline check: {len(c["baseline_members"])} member(s) x {len(refs)} references',flush=True)
    for name in c['baseline_members']:
        a = state['policies'][state['current'][name]]
        payoffs = {}
        for ref_name,b in refs.items():
            rows,_ = pair_profiles(c,a,b,c['games'],contract['seed'])
            payoffs[ref_name] = {'score':sum(r['score'] for r in rows)/len(rows),'games':len(rows)}
        score = sum(p['score'] for p in payoffs.values())/len(payoffs)
        report['members'][name] = {'checkpoint':state['current'][name], 'score':score,'payoffs':payoffs}
        print(f'{name} vs fixed initial panel: {score:.3f}',flush=True)
    check_state(c,state)
    if digest(ROOT/c['eval_binary']) != contract['binary_sha256']: raise ValueError('baseline evaluator changed')
    write(folder/f'{state["round"]}_{time.time_ns()}.json',report)
    return report


def evaluate(c, directory, state, kind='full'):
    if kind not in ('full','quick'): raise ValueError('evaluation kind must be full or quick')
    games = c['games'] if kind == 'full' else c['quick_games']
    ids = sorted(state['current'])
    versions = {name:state['policies'][identity] for name,identity in state['current'].items()}
    stamp = str(time.time_ns())
    folder = directory/'evaluations'/stamp
    folder.mkdir(parents=True)
    schema = copy.deepcopy(SCHEMA)
    for key, weight in c['descriptor_weights'].items(): schema[key]['weight'] = weight
    contract = {'ids': versions, 'checkpoints':dict(state['current']), 'games': games, 'kind':kind,
                'seed': c['seed'], 'max_updates': c['max_updates'], 'binary': digest(ROOT/c['eval_binary']), 'schema': schema,
                'pair_protocol':'unique-pairs-both-profiles-v2'}
    panel = hashlib.sha256(json.dumps(contract, sort_keys=True).encode()).hexdigest()
    records = []
    episodes = {i:[] for i in ids}
    payoffs = {i:{} for i in ids}
    pairs = len(ids)*(len(ids)-1)//2
    print(f'{kind.capitalize()} evaluation: {pairs} unique pairs x {games} games = {pairs*games} games',flush=True)
    for index,i in enumerate(ids):
        for j in ids[index+1:]:
            rows_a, rows_b = pair_profiles(c,versions[i],versions[j],games,c['seed'])
            score = sum(r['score'] for r in rows_a)/games
            payoffs[i][j] = {'score':score,'games':games}
            payoffs[j][i] = {'score':1-score,'games':games}
            episodes[i].extend(rows_a)
            episodes[j].extend(rows_b)
            print(f'{i} vs {j}: {score:.3f}',flush=True)
    for i in ids:
        records.append({'id':i,'descriptors':aggregate(episodes[i]),'payoffs':payoffs[i]})
    check_state(c, state)
    if digest(ROOT/c['eval_binary']) != contract['binary']: raise ValueError('evaluator changed during evaluation')
    data = {'version':2, 'schema_id':'pokemon-league-pairs-v2', 'schema':schema, 'panel_id': panel, 'records':records, 'evaluation':contract,
            'note':'Each member profile averages games vs other members, excluding itself; not a common-panel archive bundle.'}
    # Named members are permanent. Descriptors are diagnostics, never admission
    # criteria. Checkpoint versions do not enter the metagame as extra players.
    pool = ids
    scores = {r['id']:r['payoffs'] for r in records}
    matrix = [[scores[i][j]['score'] - scores[j][i]['score'] if i != j else 0 for j in pool] for i in pool]
    solution = metagame.solve(matrix, c['solver_iterations'], c['uniform_mix'])
    solution['solver_weights'] = solution['weights']
    solution['weights'] = opponent_weights(c,pool,solution['solver_weights'])
    solution['empirical_exploitability'] = max(0,max(sum(row[j]*solution['weights'][j] for j in range(len(pool))) for row in matrix))
    report = {'pool':pool, 'checkpoints':dict(state['current']), 'matrix':matrix, **solution, 'panel_id':panel, 'kind':kind, 'games':games,
              'settings':evaluation_settings(c)}
    write(folder/'profiles.json', data)
    write(folder/'mixture.json', report)
    state['evaluation'] = str((folder/'mixture.json').resolve())
    write(directory/'state.json', state, replace=True)
    export_native(c,directory,state)
    print('Opponent mixture: ' + ', '.join(f'{i}={w:.1%}' for i,w in zip(pool,solution['weights'])))
    print(f'Empirical pool exploitability: {solution["empirical_exploitability"]:.4f}; not a full-game bound')
    return report


def train_command(c, directory, run_id, learner, opponent, seed):
    opts = dict(c['train_overrides'])
    opts.update(c['vec_overrides'])
    opts.update({'base.run_id': run_id, 'base.seed':seed,
        'base.load_model_path': learner['path'], 'base.load_enemy_model_path':'None',
        'base.checkpoint_dir':str((directory/'training').resolve()),
        'base.log_dir':str((directory/'logs').resolve()), 'base.checkpoint_interval':100,
        'policy.hidden_size':learner['hidden'], 'policy.num_layers':learner['layers'],
        'vec.num_frozen_banks':1, 'vec.frozen_bank_pct':1, 'vec.seat_balance':0,
        'vec.frozen_bank_hidden_size':opponent['hidden'], 'vec.frozen_bank_num_layers':opponent['layers'],
        'selfplay.enabled':1, 'selfplay.opponent_pool':opponent['path'],
        'selfplay.opponent_league':'None', 'selfplay.opponent_pool_weights':'1',
        'selfplay.opponent_pool_prob':1, 'selfplay.eval_pool_size':0,
        'selfplay.seed':seed, 'env.seed':seed, 'env.team_selection':1,
        'env.native_league':'None',
        'env.learner_team':team_arg(learner['team']), 'env.opponent_team':team_arg(opponent['team']),
        'env.team_log_interval':0, 'env.max_updates':c['max_updates'],
        'train.total_timesteps':c['chunk_steps'], 'train.epoch_sampling':1,
        'train.prio_alpha':0, 'train.prio_beta0':0})
    return ['./puffer','train','pokemon',*[f'{k}={v}' for k,v in opts.items()]]


def train_round(c, directory, state, execute):
    if not state['evaluation']: raise ValueError('run eval before train/run')
    report = read(state['evaluation'])
    if report['settings'] != evaluation_settings(c): raise ValueError('evaluation settings/binary changed; run eval again')
    pool, weights = report['pool'], report['weights']
    if set(pool) != set(state['current']) or report['checkpoints'] != state['current']:
        raise ValueError('roster/checkpoints changed; evaluate current members again')
    rng = random.Random(c['seed'] + state['round'])
    # Freeze current versions of the named roster while every learner improves.
    # Journal each subprocess so interrupted rounds cannot partially advance it.
    journal = directory/'pending.json'
    if execute and journal.exists(): raise ValueError('interrupted round: inspect pending.json; see LEAGUE.md recovery')
    updated = copy.deepcopy(state)
    for name, identity in state['current'].items():
        learner = state['policies'][identity]
        eligible = [i for i, member in enumerate(pool) if member != name]
        if not eligible: raise ValueError('at least two named members required')
        conditioned = opponent_weights(c,pool,report.get('solver_weights',weights),exclude=name)
        draw_weights = [conditioned[i] for i in eligible]
        for chunk in range(c['chunks_per_member']):
            opponent_name = pool[rng.choices(eligible, weights=draw_weights)[0]]
            opponent = state['policies'][state['current'][opponent_name]]
            seed = rng.randrange(1, 2**30)
            run_id = f'league_{state["round"]+1}_{name}_{chunk}_{time.time_ns()%1000000000}'
            cmd = train_command(c, directory, run_id, learner, opponent, seed)
            print(f'{name}: chunk {chunk+1}/{c["chunks_per_member"]} vs {opponent_name}', flush=True)
            if not execute:
                print(shlex.join(cmd))
                continue
            write(journal, {'round':state['round']+1, 'member':name, 'chunk':chunk, 'command':cmd, 'partial_state':updated}, replace=True)
            subprocess.run(cmd, check=True)
            output = directory/'training'/'pokemon'/run_id
            checkpoints = sorted(output.glob('*.bin'))
            if not checkpoints: raise ValueError(f'no checkpoint produced in {output}')
            new_id = run_id
            learner = snapshot(directory, new_id, checkpoints[-1], state['assignments'][name], c['catalog_sha'])
            updated['policies'][new_id] = learner
            updated['current'][name] = new_id
    if execute:
        updated['round'] += 1
        updated['evaluation'] = None
        write(directory/'state.json', updated, replace=True)
        journal.rename(directory/f'round_{updated["round"]}_journal.json')
        print('Round saved. Evaluate updated named members before the next round.')
    return updated


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', default='ocean/pokemon/league.json')
    sub = p.add_subparsers(dest='command', required=True)
    init = sub.add_parser('init', help='initialize the fixed named roster')
    source = init.add_mutually_exclusive_group(required=True)
    source.add_argument('--checkpoint')
    source.add_argument('--fresh', action='store_true', help='independent native random weights; no old checkpoints')
    evaluate_parser = sub.add_parser('eval', help='full unique-pair evaluation (or --quick)')
    evaluate_parser.add_argument('--quick',action='store_true')
    sub.add_parser('baseline',help='evaluate configured members against immutable initial snapshots')
    train = sub.add_parser('train', help='one round; prints commands unless --execute')
    train.add_argument('--execute', action='store_true')
    run = sub.add_parser('run', help='evaluate, train and reevaluate each round (executes training)')
    run.add_argument('--rounds', type=int, default=1)
    sets = sub.add_parser('sets', help='show sourced movesets for editing teams')
    sets.add_argument('species')
    sub.add_parser('status')
    sub.add_parser('export',help='publish current checkpoints/teams for ./puffer train pokemon')
    sub.add_parser('sync', help='initialize newly configured members; preserve all existing members and weights')
    sub.add_parser('recover', help='archive interrupted-round journal; keep files, restart from last committed round')
    watch = sub.add_parser('watch')
    watch.add_argument('member', default='master', nargs='?')
    watch.add_argument('--opponent', default='master')
    args = p.parse_args()
    os.chdir(ROOT)
    if args.command == 'sets':
        for s in read('ocean/pokemon/data/catalog.json')['sets']:
            if s['species'].lower() == args.species.lower(): print(f'{s["id"]}: {s["species"]} | {" / ".join(s["moves"])}')
        return
    c = load_config(args.config)
    directory = Path(c['directory']).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    with (directory/'.lock').open('a') as lock:
        # Readers use the last atomically committed state and immutable files;
        # watching a policy must not prevent training another round.
        if args.command not in ('status', 'watch'):
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.command == 'init': return initialize(c, directory, args.checkpoint)
        state = read(directory/'state.json')
        if args.command == 'sync':
            if (directory/'pending.json').exists(): raise ValueError('recover interrupted training before sync')
            return sync_members(c,directory,state)
        check_state(c, state)
        if args.command == 'status':
            print(f'Round {state["round"]}; {len(state["current"])} named policies; {len(state["policies"])} checkpoint versions')
            for name, identity in state['current'].items(): print(f'{name}: {identity}; team={team_arg(state["assignments"][name])}')
            print(f'Evaluation: {state["evaluation"] or "needed"}')
        elif args.command == 'eval': evaluate(c, directory, state, 'quick' if args.quick else 'full')
        elif args.command == 'baseline': baseline_check(c,directory,state)
        elif args.command == 'export': export_native(c,directory,state)
        elif args.command == 'train': train_round(c, directory, state, args.execute)
        elif args.command == 'recover':
            (directory/'pending.json').rename(directory/f'interrupted_{time.time_ns()}.json')
            print('Journal archived; no checkpoints deleted. Next train restarts from the last committed round.')
        elif args.command == 'run':
            run_rounds(c,directory,state,args.rounds)
        elif args.command == 'watch':
            a,b = [state['policies'][state['current'][n]] for n in (args.member,args.opponent)]
            subprocess.run(['./pokemon','watch',a['path'],b['path']], check=True)


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        sys.exit(f'League error: {error}')
