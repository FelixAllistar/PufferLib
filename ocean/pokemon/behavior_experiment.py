#!/usr/bin/env python3
"""Fixed manual event objectives. No search, admission, or champion selection.

Run from the repository root. A failed training attempt is preserved; resuming
requires either its trained.json or a new output directory (no silent retrain).
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import numpy as np
from qd import NAMES, digest, dump, execute, read_ini, snapshot, evaluate

CONDITIONS = {'control': [0, 0], 'sleep_plus': [.5, 0], 'sleep_minus': [-.5, 0],
              'paralysis_plus': [0, .5], 'paralysis_minus': [0, -.5]}


def train_command(args, parent, job, label, seed, weights):
    arch = read_ini(Path(parent).parent/'config.ini')['policy']
    options = {
        'base.run_id': label, 'base.load_model_path': parent,
        'base.checkpoint_dir': job/'checkpoints', 'base.log_dir': job/'logs',
        'base.seed': seed, 'base.async': 0, 'base.load_enemy_model_path': 'None',
        'base.checkpoint_interval': 10,
        'env.native_league': 'None', 'env.learner_team': 'None', 'env.opponent_team': 'None',
        'env.team_selection': 1, 'env.max_updates': 512, 'env.reward_win': 1,
        'env.reward_hp_scale': 0, 'env.reward_ko_scale': 0,
        'env.behavior_sleep': weights[0], 'env.behavior_paralysis': weights[1],
        'train.total_timesteps': args.train_steps, 'train.horizon': 512,
        'train.gae_lambda': .995, 'train.anneal_lr': 0, 'train.anneal_ent_coef': 0,
        'train.emag_kl_coef': 0, 'train.reward_clip': 0, 'train.epoch_sampling': 1,
        'train.prio_alpha': 0, 'train.learning_rate': .0001, 'train.ent_coef': .0005,
        'selfplay.enabled': 1, 'selfplay.opponent_pool': 'None',
        'selfplay.opponent_league': 'None', 'selfplay.opponent_pool_weights': 'None',
        'selfplay.opponent_pool_prob': 0, 'selfplay.eval_pool_size': 0,
        'selfplay.max_size': 16, 'selfplay.opp_timeout_steps': 1000000, 'selfplay.seed': seed,
        'vec.total_agents': 1024, 'vec.seat_balance': 0,
        'vec.num_frozen_banks': 2, 'vec.frozen_bank_pct': .5,
    }
    options.update({f'env.personality_{name}': 0 for name in NAMES})
    for key in ('hidden_size', 'num_layers'):
        options[f'policy.{key}'] = arch[key]
        options[f'vec.frozen_bank_{key}'] = arch[key]
    return [args.trainer, 'train', 'pokemon'] + [f'--{k}={v}' for k, v in options.items()]


def event_summary(directory):
    rows = [json.loads(line[11:]) for log in sorted(directory.glob('panel_*.log'))
            for line in log.read_text().splitlines() if line.startswith('PK_PROFILE ')]
    if not rows or any(r.get('behavior_version') != 1 for r in rows):
        raise ValueError('event-capable evaluator required')
    result = {}
    for key in ('behavior_events', 'opponent_behavior_events'):
        values = np.asarray([r[key] for r in rows], dtype=float)
        if (values.shape != (len(rows), 2) or not np.all(np.isfinite(values))
                or np.any(values < 0) or np.any(values > [1, 6])
                or np.any(values != np.floor(values))):
            raise ValueError('invalid event metrics')
        result[key] = values.mean(axis=0).tolist()
        result[key+'_capped'] = (np.minimum(values, [1, 3])/[1, 3]).mean(axis=0).tolist()
    result['event_order'] = ['early_opponent_sleep', 'distinct_opponents_paralyzed']
    return result


def summarize(results):
    summary = {'completed_jobs': len(results), 'conditions': {},
               'note': 'Fixed manual conditions; win score and events reported separately. '
                       'Three training seeds are exploratory, not proof of a reliable effect.'}
    controls = {r['seed']: r for r in results if r['condition'] == 'control'}
    for name in CONDITIONS:
        group = [r for r in results if r['condition'] == name]
        if not group:
            continue
        item = {'seeds': [r['seed'] for r in group]}
        for key in ('quality', 'behavior_events', 'opponent_behavior_events'):
            values = np.asarray([r[key] for r in group])
            item[key+'_mean'] = values.mean(axis=0).tolist()
            item[key+'_by_seed'] = values.tolist()
            paired = [np.asarray(r[key])-np.asarray(controls[r['seed']][key])
                      for r in group if r['seed'] in controls]
            if paired:
                item[key+'_matched_control_deltas'] = np.asarray(paired).tolist()
                item[key+'_matched_control_delta_mean'] = np.mean(paired, axis=0).tolist()
        summary['conditions'][name] = item
    return summary


def run(args):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    with open(out/'run.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        parent = snapshot(args.parent, out/'parent')
        source = json.loads(Path(args.panel).read_text())
        panel = []
        for i, ref in enumerate(source['opponents']):
            if digest(ref['path']) != ref['sha256'] or digest(Path(ref['path']).parent/'config.ini') != ref['config_sha256']:
                raise ValueError('source panel fingerprint mismatch')
            panel.append({'path': snapshot(ref['path'], out/'panel'/str(i)), 'team': 'None'})
        files = [args.trainer, args.evaluator, args.panel, parent,
                 str(Path(parent).parent/'config.ini'), 'config/default.ini', 'config/pokemon.ini']
        files += [str(p) for p in Path('ocean/pokemon').glob('*.h')]
        files += ['ocean/pokemon/bridge.zig', 'ocean/pokemon/behavior_experiment.py',
                  'ocean/pokemon/qd.py', 'ocean/pokemon/cma_qd.py', 'ocean/pokemon/data/catalog.json']
        for ref in panel:
            files += [ref['path'], str(Path(ref['path']).parent/'config.ini')]
        contract = {'args': vars(args), 'conditions': CONDITIONS,
                    'files': {str(Path(p).resolve()): digest(p) for p in files}}
        manifest = out/'manifest.json'
        if manifest.exists() and json.loads(manifest.read_text()) != contract:
            raise ValueError('run inputs changed; use a new output directory')
        dump(manifest, contract)
        dump(out/'panel.json', panel)
        results = []
        for seed in args.seeds:
            for name, weights in CONDITIONS.items():
                label = f'seed{seed}_{name}'
                job = out/label
                job.mkdir(exist_ok=True)
                trained = job/'trained.json'
                if trained.exists():
                    meta = json.loads(trained.read_text())
                else:
                    log = job/'train.log'
                    if log.exists():
                        raise ValueError(f'incomplete training in {job}; inspect failure, use fresh output')
                    cmd = train_command(args, parent, job, label, seed, weights)
                    dump(job/'command.json', cmd)
                    execute(cmd, log)
                    matches = re.findall(r'PK_BEHAVIOR_CONFIG v=1 weights=([^ ]+) early_turns=5 paralysis_cap=3', log.read_text())
                    if not matches or [float(x) for x in matches[-1].split(',')] != weights:
                        raise ValueError('trainer event reward handshake mismatch')
                    checkpoints = sorted((job/'checkpoints'/'pokemon'/label).glob('*.bin'))
                    if not checkpoints:
                        raise ValueError('missing checkpoint')
                    checkpoint = str(checkpoints[-1])
                    cfg = read_ini(Path(checkpoint).parent/'config.ini')
                    for flag in cmd[3:]:
                        key, value = flag[2:].split('=', 1)
                        section, key = key.split('.', 1)
                        saved = cfg[section][key]
                        if saved != value:
                            try:
                                assert float(saved) == float(value)
                            except (ValueError, AssertionError):
                                raise ValueError(f'saved config mismatch: {section}.{key}: {saved} != {value}')
                    meta = {'condition': name, 'seed': seed, 'weights': weights, 'parent': parent,
                            'checkpoint': checkpoint, 'checkpoint_sha256': digest(checkpoint),
                            'config_sha256': digest(Path(checkpoint).parent/'config.ini')}
                    dump(trained, meta)
                if digest(meta['checkpoint']) != meta['checkpoint_sha256'] or digest(Path(meta['checkpoint']).parent/'config.ini') != meta['config_sha256']:
                    raise ValueError('trained checkpoint changed')
                result = job/'result.json'
                if not result.exists():
                    directory = job/'evaluation'
                    report = evaluate(args.evaluator, meta['checkpoint'], panel, args.games, 8001, 512, directory)
                    report.update(event_summary(directory))
                    report.update(meta)
                    if any(digest(p) != sha for p, sha in contract['files'].items()):
                        raise ValueError('inputs changed during run')
                    dump(result, report)
                results.append(json.loads(result.read_text()))
                dump(out/'summary.json', summarize(results))
                print(f'Completed {label}: win score {results[-1]["quality"]:.3f}; events {results[-1]["behavior_events"]}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', required=True)
    parser.add_argument('--panel', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--trainer', default='./build/pokemon/puffer_behavior')
    parser.add_argument('--evaluator', default='./build/pokemon/pokemon_behavior')
    parser.add_argument('--train-steps', type=int, default=100_000_000)
    parser.add_argument('--games', type=int, default=64)
    parser.add_argument('--seeds', nargs='+', type=int, default=[101, 102, 103])
    args = parser.parse_args()
    if args.train_steps < 1048576 or args.games < 2 or len(set(args.seeds)) != len(args.seeds):
        parser.error('need >=1048576 steps, >=2 games, and unique seeds')
    run(args)
