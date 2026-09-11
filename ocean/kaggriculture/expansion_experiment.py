#!/usr/bin/env python3
"""Frozen six-trial reward experiment. No promotions, submissions, or active INI edits."""
import argparse
import configparser
import csv
import datetime
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

from eval_observation_versions import observation_version
from policy_identity import identity, digest
from sweep_macro_memory import cli_value, is_oom

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SECTIONS = ('base', 'policy', 'torch', 'vec', 'selfplay', 'env', 'train', 'league')


def save(path, value):
    path = Path(path)
    temp = path.with_name(path.name + '.pending')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def config_values():
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read([ROOT / 'config/default.ini', ROOT / 'config/kaggriculture.ini'])
    return {f'{section}.{key}': cli_value(value)
            for section in SECTIONS if config.has_section(section)
            for key, value in config.items(section)}


def trial_grid(seeds):
    return [dict(seed=seed, deadline=deadline, expansion=scale)
            for seed in seeds for deadline in (240, 300, 360) for scale in (3, 10)]


def frozen_values(base, out):
    values = dict(base)
    # Turn off every independent reward branch; fitted constants are inert.
    for key in values:
        if key.startswith('env.reward_') and (key.endswith('_scale') or key == 'env.reward_progress_health_ratio'):
            values[key] = '0'
    values.update({
        'env.reward_progress_terminal_money_scale': '4',
        'env.reward_progress_win_scale': '1',
        'env.curriculum_enabled': '0', 'env.curriculum_reward': '0',
        'env.reset_state_prob': '0', 'env.reset_state_bank': 'None',
        'env.reset_opening_prob': '0', 'env.reset_opening_turns': '0', 'env.opening_turns': '0',
        'base.load_model_path': 'None', 'base.load_enemy_model_path': 'None',
        'base.result_fd': '0', 'base.wandb': '0',
        'base.checkpoint_dir': str(out / 'checkpoints'), 'base.log_dir': str(out / 'training_logs'),
        'selfplay.enabled': '1', 'selfplay.opponent_league': str(out / 'league/league.ini'),
        'selfplay.opponent_pool': 'None', 'selfplay.opponent_pool_weights': 'None',
        'selfplay.opponent_pool_prob': '1', 'selfplay.magnet_path': 'None',
        'selfplay.eval_pool_size': '0', 'selfplay.snapshot_interval': str(10**12),
        'selfplay.pfsp_mode': 'variance', 'selfplay.pfsp_alpha': '0',
        'selfplay.pfsp_uniform_mix': '0',
    })
    return values


def argv(mode, values):
    return [str(ROOT / 'puffer'), mode, 'kaggriculture'] + [f'{k}={v}' for k, v in sorted(values.items())]


def build_probe():
    target = HERE / 'build/libkag_experiment.so'
    target.parent.mkdir(exist_ok=True)
    command = ['cc', '-O2', '-std=c17', '-fPIC', '-shared', '-I.', '-Isrc', '-Ivendor',
               '-Iraylib-5.5_linux_amd64/include', str(HERE / 'kag_experiment_view.c'),
               'raylib-5.5_linux_amd64/lib/libraylib.a', '-lGL', '-lm', '-lpthread', '-o', str(target)]
    subprocess.run(command, cwd=ROOT, check=True)
    return target


def prepare(out, league, seeds, steps, games):
    if out.exists():
        raise ValueError('Output exists: choose a new name or use run to resume')
    base = config_values()
    for key, expected in {'env.macro_mode':'2', 'env.macro_decision_interval':'1',
                          'env.macro_score_scale':'10000', 'env.policy_max_hands':'240',
                          'env.policy_market_slots':'10', 'env.episode_steps':'720',
                          'env.board_size':'10', 'env.turns_per_day':'24'}.items():
        if float(base.get(key, 'nan')) != float(expected):
            raise ValueError(f'Behavior probe requires {key}={expected}; got {base.get(key)}')
    config = configparser.ConfigParser(interpolation=None)
    if not config.read(league):
        raise ValueError(f'Missing league: {league}')
    records, seen = [], set()
    for section in config.sections():
        if not section.startswith('policy.') or not config.getboolean(section, 'enabled', fallback=True):
            continue
        source = Path(cli_value(config[section]['path']))
        if not source.is_absolute(): source = ROOT / source
        key = identity(source)
        if key in seen: continue
        seen.add(key)
        records.append(dict(source=str(source), identity=key,
                            version=observation_version(source), weight=float(config[section]['train_weight'])))
    if not 2 <= len(records) <= 8 or len({r['version'] for r in records}) != 1:
        raise ValueError('Expected 2..8 unique same-layout opponents')
    if records[0]['version'] != int(base['env.observation_version']):
        raise ValueError('Training layout differs from frozen league')
    probe = build_probe()
    out.mkdir(parents=True)
    (out / 'league').mkdir()
    frozen = configparser.ConfigParser(interpolation=None)
    frozen['league'] = {'max_active': str(len(records))}
    total_weight = sum(r['weight'] for r in records)
    if total_weight <= 0: raise ValueError('Invalid league weights')
    for i, record in enumerate(records):
        dest = out / 'league' / f'opponent_{i:02d}_{record["identity"].split(":")[-1][:12]}.bin'
        shutil.copy2(record['source'], dest)
        Path(str(dest) + '.obs_version').write_text(str(record['version']) + '\n')
        record.update(checkpoint=str(dest), sha256=digest(dest), weight=record['weight']/total_weight)
        frozen[f'policy.opponent_{i:02d}'] = dict(path=str(dest), train_weight=str(record['weight']), enabled='1')
    with (out / 'league/league.ini').open('w') as stream: frozen.write(stream)
    values = frozen_values(base, out)
    values['env.frozen_observation_version'] = str(records[0]['version'])
    artifacts = [ROOT / 'puffer', HERE / 'expansion_experiment.py', HERE / 'experiment_behavior.py',
                 HERE / 'eval_observation_versions.py', HERE / 'policy_identity.py', probe,
                 HERE / 'submission/main.py', HERE / 'replay_native.py', out / 'league/league.ini']
    plan = dict(format='kag_expansion_experiment_v1', created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                start='fresh', steps=steps, eval_games=games, eval_seed=81001,
                behavior_seeds=[91001], base=base, values=values, opponents=records,
                artifacts={str(p):digest(p) for p in artifacts}, trials=trial_grid(seeds),
                note='Behavior games are CPU-native diagnostic samples; competitive scores use GPU native evaluator. No automatic promotion.')
    save(out / 'plan.json', plan)
    (out / 'plan.sha256').write_text(digest(out / 'plan.json') + '\n')
    save(out / 'state.json', {'trials':[{'status':'pending', 'attempts':[]} for _ in plan['trials']]})
    print(f'Prepared {len(plan["trials"])} fresh trials at {steps:,} steps each: {out}', flush=True)
    print('Frozen optimizer:', {k:v for k,v in values.items() if k in
          ('train.learning_rate','train.gae_lambda','train.emag_kl_coef','train.emag_tau','train.horizon','train.minibatch_size','vec.total_agents')}, flush=True)


def execute(command, log, checkpoint_dir=None, on_checkpoint=None):
    save(log.with_suffix('.command.json'), command)
    elapsed, last_notice = time.monotonic(), 0
    child = None
    observed = set()
    try:
        with log.open('w') as stream:
            child = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True,
                                     env=dict(os.environ, OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1', PYTHONUNBUFFERED='1'))
            while child.poll() is None:
                if checkpoint_dir:
                    for checkpoint in sorted(checkpoint_dir.glob('[0-9]'*16 + '.bin')):
                        if checkpoint not in observed:
                            observed.add(checkpoint)
                            on_checkpoint(checkpoint, time.monotonic()-elapsed)
                if time.monotonic() - last_notice >= 30:
                    last_notice = time.monotonic()
                    print(f'  running {log.name} elapsed={last_notice-elapsed:.0f}s checkpoints={len(observed)}', flush=True)
                time.sleep(1)
            return child.returncode, time.monotonic()-elapsed
    except BaseException:
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGINT)
            try: child.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGTERM)
                child.wait(timeout=15)
        raise


def evaluate_checkpoint(out, plan, attempt, checkpoint, index):
    import statistics
    results = []
    for deterministic in (1, 0):
        prefix = Path(attempt['directory']) / f'eval_{checkpoint.stem}_d{deterministic}'
        result_path = prefix.with_suffix('.json')
        if result_path.exists():
            results.append(json.loads(result_path.read_text()))
            continue
        candidate = prefix.with_suffix('.candidates.tsv')
        opponents = out / 'opponents.tsv'
        candidate.write_text(f'id\tpolicy\tcheckpoint\n0\ttrial_{index}\t{checkpoint}\n')
        opponents.write_text('id\tpolicy\tcheckpoint\n' + ''.join(
            f'{i}\topponent_{i}\t{r["checkpoint"]}\n' for i,r in enumerate(plan['opponents'])))
        raw = prefix.with_suffix('.matches.tsv')
        values = dict(plan['values'])
        values.update({'base.run_id': f'{attempt["run_id"]}_eval_d{deterministic}',
                       'base.eval_deterministic': str(deterministic), 'base.seed':str(plan['eval_seed']),
                       'env.seed':str(plan['eval_seed']), 'train.total_timesteps':'0',
                       'train.horizon':'8', 'vec.total_agents':'64', 'selfplay.enabled':'0'})
        command = [sys.executable, str(HERE / 'eval_observation_versions.py'), 'screen',
                   '--candidates', str(candidate), '--opponents', str(opponents),
                   '--output', str(raw), '--games', str(plan['eval_games']), '--'] + [f'{k}={v}' for k,v in values.items()]
        rc, seconds = execute(command, prefix.with_suffix('.log'))
        if rc: raise RuntimeError(f'Competitive evaluation failed: {prefix}')
        rows = [line.split('\t') for line in raw.read_text().splitlines() if line.strip()]
        if len(rows) != len(plan['opponents']): raise ValueError('Incomplete opponent panel')
        score = statistics.mean(float(r[2]) for r in rows)
        money = statistics.mean(float(r[4]) for r in rows)
        margin = statistics.mean(float(r[4])-float(r[5]) for r in rows)
        behavior = prefix.with_suffix('.behavior.json')
        env_json = out / 'behavior_env.json'
        save(env_json, {k[4:]:v for k,v in plan['values'].items() if k.startswith('env.')})
        command = [sys.executable, str(HERE/'experiment_behavior.py'), '--model', str(checkpoint),
                   '--opponents', *[r['checkpoint'] for r in plan['opponents']],
                   '--seeds', *map(str, plan['behavior_seeds']), '--output', str(behavior), '--env-json', str(env_json)]
        if not deterministic: command.append('--stochastic')
        rc, behavior_seconds = execute(command, prefix.with_suffix('.behavior.log'))
        if rc: raise RuntimeError(f'Behavior evaluation failed: {prefix}')
        result = dict(trial=index, run_id=attempt['run_id'], checkpoint=str(checkpoint), sha256=digest(checkpoint),
                      steps=int(checkpoint.stem), training_seconds=attempt['checkpoint_seconds'].get(checkpoint.name, attempt['train_seconds']),
                      mode='deterministic' if deterministic else 'stochastic', score=score, money=money, margin=margin,
                      eval_seconds=seconds, behavior_seconds=behavior_seconds,
                      behavior=json.loads(behavior.read_text())['summary'])
        save(result_path, result)
        results.append(result)
        print(f'RESULT trial={index} {result["mode"]} steps={result["steps"]:,} money={money:.0f} score={score:.3f}', flush=True)
        report(out)
    return results


def report(out):
    plan = json.loads((out/'plan.json').read_text())
    rows = []
    for path in out.glob('trial_*/attempt_*/eval_*_d[01].json'):
        record = json.loads(path.read_text())
        row = {k:record[k] for k in ('trial','run_id','steps','training_seconds','mode','score','money','margin')}
        row.update(plan['trials'][record['trial']])
        row.update(record['behavior'])
        rows.append(row)
    rows.sort(key=lambda r:(r['trial'],r['run_id'],r['mode'],r['steps']))
    if not rows: return
    with (out/'summary.tsv').open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter='\t', lineterminator='\n')
        writer.writeheader(); writer.writerows(rows)
    save(out/'learning_curves.json', rows)
    lines = ['# Early expansion experiment', '',
             'GPU match scores and CPU-native behavior diagnostics use separate fixed seed panels.',
             'Training time excludes evaluation. Compare modes separately; no automatic winner or promotion.', '',
             '| Trial | Deadline / scale | Mode | Steps M | Train min | Money | Score | Cow ≤150 | Milk ≤300 harvested | Productive extra tile-turns |',
             '|---|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        lines.append(f'| {r["trial"]} | {r["deadline"]} / {r["expansion"]} | {r["mode"]} | {r["steps"]/1e6:.1f} | {r["training_seconds"]/60:.1f} | {r["money"]:.0f} | {r["score"]:.3f} | {r["cow_by_150_fraction"]:.1%} | {r["milk_by_300"]:.1f} | {r["productive_extra_tile_turns"]:.0f} |')
    (out/'report.md').write_text('\n'.join(lines)+'\n')
    # Standalone SVG curves: one series per trial/mode, against steps and training time.
    for xkey, label in [('steps','Environment steps'), ('training_seconds','Training seconds')]:
        xmax = max(r[xkey] for r in rows) or 1
        ymax = max(r['money'] for r in rows) or 1
        svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="900" height="420" viewBox="0 0 900 420">',
               '<rect width="900" height="420" fill="white"/>',
               f'<text x="50" y="22">Final money versus {label}; solid = deterministic, dashed = stochastic</text>',
               '<path d="M50 40 V350 H850" stroke="black" fill="none"/>']
        colors = ['#0072B2','#D55E00','#009E73','#CC79A7','#E69F00','#444444']
        for trial in range(len(plan['trials'])):
            color = colors[trial % len(colors)]
            for mode in ('deterministic','stochastic'):
                points = [r for r in rows if r['trial']==trial and r['mode']==mode]
                if not points: continue
                points.sort(key=lambda r:r[xkey])
                xy = [(50+800*r[xkey]/xmax, 350-300*r['money']/ymax) for r in points]
                dash = ' stroke-dasharray="6 4"' if mode=='stochastic' else ''
                coords = ' '.join(f'{x:.1f},{y:.1f}' for x,y in xy)
                svg.append(f'<polyline points="{coords}" fill="none" stroke="{color}"{dash}/>')
                for x,y in xy: svg.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3" fill="{color}"/>')
            svg.append(f'<text x="{50+130*(trial%6)}" y="400" fill="{color}">Trial {trial}</text>')
        svg += [f'<text x="50" y="372">0</text><text x="720" y="372">{xmax:.0f}</text>',
                f'<text x="5" y="48">{ymax:.0f}</text>', '</svg>']
        (out/f'curves_{xkey}.svg').write_text('\n'.join(svg))


def run(out, max_trials=0):
    if digest(out/'plan.json') != (out/'plan.sha256').read_text().strip():
        raise ValueError('Experiment plan changed; use a new output')
    plan = json.loads((out/'plan.json').read_text())
    for path, expected in plan['artifacts'].items():
        if digest(path) != expected: raise ValueError(f'Frozen artifact changed: {path}')
    for record in plan['opponents']:
        if identity(record['checkpoint']) != record['identity']: raise ValueError('Frozen opponent changed')
    state = json.loads((out/'state.json').read_text())
    completed_this_call = 0
    with (out/'queue.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for index, params in enumerate(plan['trials']):
            trial = state['trials'][index]
            if trial['status'] in ('complete','oom','failed'): continue
            try:
                if trial['status'] != 'evaluating':
                    if trial['attempts'] and trial['status']=='training':
                        trial['attempts'][-1]['status']='interrupted'
                    attempt_id = len(trial['attempts'])
                    run_id = f'{out.name}_t{index:02d}_a{attempt_id}'
                    directory = out/f'trial_{index:02d}'/f'attempt_{attempt_id}'
                    directory.mkdir(parents=True, exist_ok=False)
                    checkpoint_dir = out/'checkpoints/kaggriculture'/run_id
                    checkpoint_dir.mkdir(parents=True, exist_ok=False)
                    values = dict(plan['values'])
                    interval = max(1, round(100000000/(int(values['vec.total_agents'])*int(values['train.horizon']))))
                    values.update({'base.run_id':run_id, 'train.total_timesteps':str(plan['steps']),
                                   'base.checkpoint_interval':str(interval),
                                   'env.reward_expansion_deadline':str(params['deadline']),
                                   'env.reward_expansion_scale':str(params['expansion'])})
                    for key in ('base.seed','train.seed','env.seed','selfplay.seed'): values[key]=str(params['seed'])
                    metadata = dict(run_id=run_id, observation_version=int(values['env.observation_version']),
                                    hidden_size=int(values['policy.hidden_size']), num_layers=int(values['policy.num_layers']),
                                    macro_mode=2, start='fresh', config=values)
                    save(checkpoint_dir/'run_metadata.json', metadata)
                    save(directory/'starting_config.json', values)
                    attempt = dict(run_id=run_id, directory=str(directory), checkpoint_dir=str(checkpoint_dir),
                                   checkpoint_seconds={}, status='training')
                    trial['attempts'].append(attempt); trial['status']='training'
                    save(out/'state.json', state)
                    def observed(path, seconds):
                        attempt['checkpoint_seconds'][path.name] = seconds
                        Path(str(path)+'.obs_version').write_text(str(metadata['observation_version'])+'\n')
                        save(out/'state.json', state)
                    print(f'START trial {index+1}/{len(plan["trials"])} {params} FRESH {run_id}', flush=True)
                    rc, seconds = execute(argv('train', values), directory/'train.log', checkpoint_dir, observed)
                    attempt['train_seconds']=seconds
                    if rc:
                        attempt['status']=trial['status']='oom' if is_oom((directory/'train.log').read_text(errors='replace')) else 'failed'
                        attempt['exit_code']=rc
                        save(out/'state.json', state)
                        print(f'FAILED {run_id}: {trial["status"]}; preserved logs; advancing queue', flush=True)
                        continue
                    checkpoints = sorted(checkpoint_dir.glob('[0-9]'*16+'.bin'))
                    if not checkpoints or int(checkpoints[-1].stem) < plan['steps']-int(values['vec.total_agents'])*int(values['train.horizon']):
                        raise RuntimeError('Training ended without requested step budget')
                    for path in checkpoints:
                        if path.name not in attempt['checkpoint_seconds']: observed(path, seconds)
                    save(directory/'checkpoints.json', [{'path':str(p),'sha256':digest(p),'steps':int(p.stem)} for p in checkpoints])
                    attempt['status']=trial['status']='evaluating'
                    save(out/'state.json', state)
                attempt=trial['attempts'][-1]
                for record in json.loads((Path(attempt['directory'])/'checkpoints.json').read_text()):
                    if digest(record['path']) != record['sha256']:
                        raise ValueError(f'Checkpoint changed: {record["path"]}')
                checkpoints=sorted(Path(attempt['checkpoint_dir']).glob('[0-9]'*16+'.bin'))
                selected=[]
                for target in (plan['steps']/3, 2*plan['steps']/3, plan['steps']):
                    path=min(checkpoints, key=lambda p:abs(int(p.stem)-target))
                    if path not in selected: selected.append(path)
                for checkpoint in selected: evaluate_checkpoint(out, plan, attempt, checkpoint, index)
                attempt['status']=trial['status']='complete'
                save(out/'state.json', state)
                report(out)
                completed_this_call += 1
                if max_trials and completed_this_call >= max_trials:
                    print('Requested trial limit reached; remaining trials are resumable.', flush=True)
                    return
            except KeyboardInterrupt:
                save(out/'state.json', state)
                print('Queue paused. Completed trials/evaluations are kept; interrupted training restarts fresh under a new attempt ID.', flush=True)
                raise
        print(f'QUEUE FINISHED: {out}/report.md (failed trials remain recorded; no promotion/submission)', flush=True)


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('command', choices=['prepare','run','report'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--league', type=Path)
    parser.add_argument('--steps', type=int, default=300000000)
    parser.add_argument('--eval-games', type=int, default=64)
    parser.add_argument('--seeds', nargs='+', type=int, default=[42])
    parser.add_argument('--max-trials', type=int, default=0, help='Stop after this many completed trials in this invocation; zero runs all')
    args=parser.parse_args()
    out=args.output.resolve()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', out.name): raise ValueError('Use a simple experiment directory name')
    if args.steps < 1 or args.eval_games < 2 or args.eval_games % 2: raise ValueError('Invalid step/game budget')
    if args.command=='prepare':
        if not args.league: parser.error('prepare requires --league')
        prepare(out,args.league,args.seeds,args.steps,args.eval_games)
    elif args.command=='run':
        signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
        run(out, args.max_trials)
    else: report(out)


if __name__=='__main__':
    main()
