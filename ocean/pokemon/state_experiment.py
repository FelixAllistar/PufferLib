#!/usr/bin/env python3
"""Matched win-only root/reset experiment. Run from repository root.

Prepare immutable migrated inputs and a fixed native state bank, then train four
jobs. Existing failed attempts are never silently retrained or overwritten.
"""
import argparse
from collections import Counter
import configparser
import fcntl
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
import numpy as np

FLAG_BYTE = 476
STYLE_KEYS = ('paralysis', 'sleep', 'offense', 'defense', 'reserve')


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def read_ini(path):
    cfg = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=('#', ';'))
    with open(path) as f:
        cfg.read_file(f)
    return cfg


def dump(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n')
    temporary.replace(path)


def execute(cmd, log, environment=None):
    log = Path(log)
    if log.exists():
        raise ValueError(f'Existing attempt log: {log}; inspect failure, do not silently rerun')
    print(' '.join(map(str, cmd)), flush=True)
    start = time.monotonic()
    with log.open('x') as f:
        subprocess.run(list(map(str, cmd)), stdout=f, stderr=subprocess.STDOUT, check=True, env=environment)
    return time.monotonic() - start


def migrate(source, directory):
    """Zero ONLY the formerly reserved encoder column; root behavior is unchanged."""
    source = Path(source).resolve(strict=True)
    cfg_path = source.parent / 'config.ini'
    cfg = read_ini(cfg_path)
    if cfg.getint('env', 'abi_version') != 2 or cfg['env'].get('learner_team', 'None') != 'None':
        raise ValueError('Need unrestricted ABI-2 collectors/opponents')
    if cfg['env'].getint('reset_observation_version', 0) > 1:
        raise ValueError('Unsupported reset observation version')
    # Never erase a reset input already learned by a bank-trained policy.
    if cfg['env'].getfloat('reset_state_prob', 0) != 0:
        raise ValueError('Migration is only for pre-state-bank checkpoints')
    hidden = cfg.getint('policy', 'hidden_size')
    layers = cfg.getint('policy', 'num_layers')
    align = lambda n: (n + 7) & ~7
    expected = align(hidden * 640) + align(hidden * 161) + layers * align(3 * hidden * hidden)
    original = np.frombuffer(source.read_bytes(), dtype='<f4')
    if len(original) != expected or not np.all(np.isfinite(original)):
        raise ValueError('Incompatible checkpoint payload')
    weights = original.copy()
    weights[:hidden * 640].reshape(hidden, 640)[:, FLAG_BYTE] = 0
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / source.name
    metadata = {'source': str(source), 'source_sha256': digest(source),
                'source_config_sha256': digest(cfg_path), 'flag_byte': FLAG_BYTE,
                'flag_encoding': 'raw uint8 0/1 (Pokemon is not normalized)',
                'hidden_size': hidden, 'num_layers': layers}
    if (directory / 'migration.json').exists():
        saved = json.loads((directory / 'migration.json').read_text())
        if any(saved[k] != v for k, v in metadata.items()) or digest(target) != saved['sha256'] or digest(directory / 'config.ini') != saved['config_sha256']:
            raise ValueError('Migration inputs/output changed')
        return saved
    if target.exists() or (directory / 'config.ini').exists():
        raise ValueError('Incomplete migration; use fresh output directory')
    with target.open('xb') as f:
        f.write(weights.tobytes())
    cfg['env']['reset_observation_version'] = '1'
    cfg['env']['reset_flag_initialized'] = '1'
    cfg['env']['reset_state_prob'] = '0'
    cfg['env']['reset_state_bank'] = 'None'
    with (directory / 'config.ini').open('x') as f:
        cfg.write(f)
    metadata.update(path=str(target.resolve()), sha256=digest(target),
                    config_sha256=digest(directory / 'config.ini'))
    dump(directory / 'migration.json', metadata)
    return metadata


def fingerprints(paths):
    return {str(Path(p).resolve()): digest(p) for p in paths}


def verify_files(files):
    for path, expected in files.items():
        if digest(path) != expected:
            raise ValueError(f'Input changed: {path}')


def prepare(args, out):
    source = Path(args.source_run).resolve()
    old = json.loads((source / 'contract.json').read_text())
    warm = old['warmstarts']
    collectors = [r['checkpoint'] for r in warm]
    collectors += [str(source / 'jobs/g01_c03/checkpoints/pokemon/g01_c03/0000000099614720.bin')]
    holdout = [r['path'] for r in old['panels']['holdout']]
    for path in collectors[:len(warm)] + holdout:
        for p in (path, str(Path(path).parent / 'config.ini')):
            if digest(p) != old['files'][p]:
                raise ValueError('Original experiment input hash mismatch')
    if set(map(digest, collectors)) & set(map(digest, holdout)):
        raise ValueError('Collection/training policies overlap evaluation opponents')
    pool = [migrate(p, out / 'inputs' / f'pool{i}') for i, p in enumerate(collectors)]
    panel = [migrate(p, out / 'inputs' / f'holdout{i}') for i, p in enumerate(holdout)]
    parent = pool[0]
    cfg = read_ini(Path(parent['path']).parent / 'config.ini')
    for key in ['behavior_sleep', 'behavior_paralysis', 'reward_hp_scale', 'reward_ko_scale'] + [f'personality_{k}' for k in STYLE_KEYS]:
        if cfg['env'].getfloat(key, 0) != 0:
            raise ValueError('Parent must already have a win-only critic')
    paths = [args.collector, 'ocean/pokemon/collect_states.c', 'ocean/pokemon/state_bank.h',
             'ocean/pokemon/pokemon_core.h', 'ocean/pokemon/bridge.zig', 'ocean/pokemon/bridge.h',
             'ocean/pokemon/catalog_meta.h', 'ocean/pokemon/policy_view.h']
    paths += [p for r in pool + panel for p in (r['path'], str(Path(r['path']).parent / 'config.ini'))]
    contract = {'version': 1, 'collection_games': args.collection_games, 'collection_seed': args.collection_seed,
                'parent': parent, 'pool': pool, 'panel': panel, 'files': fingerprints(paths)}
    saved = out / 'bank_contract.json'
    if saved.exists() and json.loads(saved.read_text()) != contract:
        raise ValueError('Bank contract changed; use a new output directory')
    dump(saved, contract)
    bank = out / 'states.pks'
    meta_path = out / 'bank.json'
    if not meta_path.exists():
        if bank.exists():
            raise ValueError('Incomplete bank build; preserve it and use a new output directory')
        seconds = execute([args.collector, bank, args.collection_games, args.collection_seed,
                           *[r['path'] for r in pool]], out / 'collection.log')
        rows = [json.loads(s) for s in (out / 'collection.log').read_text().splitlines() if s.startswith('{')]
        if len(rows) != 1 or min(rows[0]['counts']) < 1:
            raise ValueError('Missing collector report')
        verify_files(contract['files'])
        meta = dict(rows[0], path=str(bank), sha256=digest(bank), wall_seconds=seconds,
                    bytes=bank.stat().st_size, bank_contract_sha256=digest(saved))
        dump(meta_path, meta)
    meta = json.loads(meta_path.read_text())
    if digest(bank) != meta['sha256'] or digest(saved) != meta['bank_contract_sha256']:
        raise ValueError('Bank fingerprint changed')
    return contract, meta


def train_options(args, out, contract, bank, seed, arm):
    parent = contract['parent']
    label = f'seed{seed}_{arm}'
    job = out / 'jobs' / label
    opts = {
        'base.run_id': label, 'base.load_model_path': parent['path'],
        'base.checkpoint_dir': job / 'checkpoints', 'base.log_dir': job / 'logs',
        'base.checkpoint_interval': 10, 'base.seed': seed, 'base.async': 0,
        'base.load_enemy_model_path': 'None', 'base.reset_every_horizon': 0,
        'env.seed': seed + 100000, 'env.native_league': 'None', 'env.learner_team': 'None',
        'env.opponent_team': 'None', 'env.team_selection': 1, 'env.max_updates': 512,
        'env.reset_state_bank': bank['path'], 'env.reset_state_prob': .5 if arm == 'states' else 0,
        'env.reset_observation_version': 1, 'env.reward_win': 1,
        'env.reward_hp_scale': 0, 'env.reward_ko_scale': 0,
        'env.behavior_sleep': 0, 'env.behavior_paralysis': 0,
        'train.total_timesteps': args.train_steps, 'train.seed': seed,
        'train.horizon': 512, 'train.minibatch_size': 8192, 'train.gamma': .999,
        'train.gae_lambda': .995, 'train.learning_rate': .0001, 'train.ent_coef': .0005,
        'train.anneal_lr': 0, 'train.anneal_ent_coef': 0, 'train.emag_kl_coef': 0,
        'train.reward_clip': 0, 'train.replay_ratio': 1, 'train.epoch_sampling': 1,
        'train.prio_alpha': 0, 'train.prio_beta0': 0,
        'selfplay.enabled': 1, 'selfplay.seed': seed, 'selfplay.max_size': 16,
        'selfplay.opponent_pool': ','.join(r['path'] for r in contract['pool']),
        'selfplay.opponent_pool_weights': ','.join('1' for _ in contract['pool']),
        'selfplay.opponent_league': 'None', 'selfplay.opponent_pool_prob': 1,
        'selfplay.pfsp_alpha': 0, 'selfplay.pfsp_uniform_mix': 1,
        'selfplay.opp_timeout_steps': 250000, 'selfplay.eval_pool_size': 0,
        'vec.total_agents': 1024, 'vec.num_threads': 4, 'vec.num_buffers': 1,
        'vec.seat_balance': 0, 'vec.num_frozen_banks': 2, 'vec.frozen_bank_pct': 1,
        'policy.hidden_size': parent['hidden_size'], 'policy.num_layers': parent['num_layers'],
        'vec.frozen_bank_hidden_size': parent['hidden_size'], 'vec.frozen_bank_num_layers': parent['num_layers'],
    }
    opts.update({f'env.personality_{k}': 0 for k in STYLE_KEYS})
    return job, opts


def check_saved_config(path, opts):
    cfg = read_ini(path)
    for key, expected in opts.items():
        section, field = key.split('.', 1)
        actual = cfg[section][field]
        if actual != str(expected):
            try:
                if float(actual) == float(expected):
                    continue
            except ValueError:
                pass
            raise ValueError(f'Saved configuration mismatch: {key}: {actual} != {expected}')


def evaluate(args, checkpoint, panel, directory):
    directory.mkdir(parents=True, exist_ok=True)
    result = directory / 'result.json'
    eval_contract = {'checkpoint_sha256': digest(checkpoint),
                     'config_sha256': digest(Path(checkpoint).parent / 'config.ini'),
                     'panel': panel, 'seeds': args.eval_seeds, 'games': args.games,
                     'evaluator_sha256': digest(args.evaluator)}
    if result.exists():
        saved = json.loads(result.read_text())
        if saved['contract'] != eval_contract:
            raise ValueError('Evaluation cache mismatch')
        return saved
    rows, payoffs = [], {}
    started = time.monotonic()
    for seed in args.eval_seeds:
        for i, opponent in enumerate(panel):
            log = directory / f'seed{seed}_opponent{i}.log'
            execute([args.evaluator, 'eval', checkpoint, opponent['path'], '--profile-json',
                     f'--seed={seed}', f'--games={args.games}', '--team-a=None', '--team-b=None',
                     'env.team_selection=1', 'env.max_updates=512', 'env.reset_state_prob=0'], log)
            match = [json.loads(s[11:]) for s in log.read_text().splitlines() if s.startswith('PK_PROFILE ')]
            if len(match) != args.games or any(r['score'] not in (0, .5, 1) for r in match):
                raise ValueError('Incomplete evaluation')
            rows += match
            payoffs[f'{seed}/{i}'] = sum(r['score'] for r in match) / len(match)
    teams = Counter(','.join(map(str, sorted(r['species_ids']))) for r in rows)
    report = {'contract': eval_contract, 'score': sum(r['score'] for r in rows) / len(rows),
              'games': len(rows), 'payoffs': payoffs, 'teams': dict(teams),
              'wall_seconds': time.monotonic() - started}
    dump(result, report)
    return report


def summarize(out, parent_score, results, bank):
    finals = [r for r in results if r['final']]
    deltas = []
    for seed in sorted({r['seed'] for r in finals}):
        pair = {r['arm']: r for r in finals if r['seed'] == seed}
        if len(pair) == 2:
            deltas.append({'seed': seed, 'score_delta': pair['states']['score'] - pair['root']['score']})
    report = {'parent_score': parent_score, 'results': results, 'paired_final_deltas': deltas,
              'bank': bank, 'note': 'Root-start held-out panel scores, not exploitability. '
              'Training seconds exclude evaluation; bank generation is a one-time treatment cost. '
              'Two seeds are a pilot, not a general guarantee.'}
    dump(out / 'report.json', report)
    lines = ['# Pokémon state-bank pilot', '', f'Parent held-out score: {parent_score:.2%}.', '',
             '| Training seed | Arm | Steps | Held-out score | Training seconds to checkpoint |',
             '|---|---|---:|---:|---:|']
    for r in results:
        lines.append(f"| {r['seed']} | {r['arm']} | {r['steps']:,} | {r['score']:.2%} | {r['train_seconds']:.1f} |")
    lines += ['', f"Bank: {bank['counts']} opening/midgame/endgame states; collection {bank['wall_seconds']:.1f}s.",
              '', 'All scores use normal drafts, both seats, fresh evaluation seeds and four held-out opponents.',
              'The training population is the same six fixed self-generated policies in both arms.',
              'Bank episodes use zero recurrent memory and a persistent auxiliary-task flag.',
              'Reset improvements must transfer to root games; reset-game win rates are not the result.']
    if deltas:
        lines += ['', 'Matched final reset-minus-root score differences: ' + ', '.join(f"seed {r['seed']}: {r['score_delta']:+.2%}" for r in deltas) + '.']
    (out / 'REPORT.md').write_text('\n'.join(lines) + '\n')


def run(args):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'run.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        contract, bank = prepare(args, out)
        if args.command == 'prepare':
            print(json.dumps(bank), flush=True)
            return
        files = list(contract['files']) + [args.trainer, args.evaluator, args.benchmark,
                'ocean/pokemon/state_experiment.py', 'ocean/pokemon/pokemon_base.h',
                'ocean/pokemon/pokemon.h', 'ocean/pokemon/pokemon.c', 'src/pufferl.cu',
                'config/pokemon.ini', 'config/default.ini', bank['path']]
        manifest = {'version': 1, 'args': vars(args), 'files': fingerprints(files),
                    'bank_contract': contract, 'bank': bank}
        saved = out / 'experiment.json'
        if saved.exists() and json.loads(saved.read_text()) != manifest:
            raise ValueError('Experiment inputs changed; use a new output directory')
        dump(saved, manifest)
        benchmark = out / 'benchmark.log'
        if not benchmark.exists():
            execute([args.benchmark, '--benchmark', bank['path']], benchmark)
        parent = evaluate(args, contract['parent']['path'], contract['panel'], out / 'evaluations' / 'parent')
        results = []
        for position, seed in enumerate(args.seeds):
            # Counterbalance execution order to reduce systematic machine-load bias.
            for arm in (('root', 'states') if position % 2 == 0 else ('states', 'root')):
                verify_files(manifest['files'])
                job, opts = train_options(args, out, contract, bank, seed, arm)
                job.mkdir(parents=True, exist_ok=True)
                meta_path = job / 'trained.json'
                if not meta_path.exists():
                    cmd = [args.trainer, 'train', 'pokemon'] + [f'--{k}={v}' for k, v in opts.items()]
                    dump(job / 'command.json', cmd)
                    started = time.time()
                    seconds = execute(cmd, job / 'train.log')
                    log = (job / 'train.log').read_text()
                    if f'external={len(contract["pool"])} banks=2 external_prob=1.00' not in log:
                        raise ValueError('Wrong opponent population')
                    if ('PK_STATE_BANK v=1' in log) != (arm == 'states'):
                        raise ValueError('State-bank training handshake mismatch')
                    if 'PK_BEHAVIOR_CONFIG v=1 weights=0,0 ' not in log:
                        raise ValueError('Wrong reward objective')
                    checkpoints = sorted((job / 'checkpoints' / 'pokemon' / opts['base.run_id']).glob('*.bin'))
                    if not checkpoints:
                        raise ValueError('Missing trained checkpoint')
                    check_saved_config(checkpoints[0].parent / 'config.ini', opts)
                    metrics = read_ini(job / 'logs' / 'pokemon' / f'{opts["base.run_id"]}.ini')['metrics']
                    telemetry = {key: [float(v) for v in metrics[key].split(',')] for key in (
                        'env/root_score', 'env/reset_score', 'env/reset_episode_fraction',
                        'env/reset_step_fraction', 'env/opening_fraction', 'env/midgame_fraction',
                        'env/endgame_fraction')}
                    if not all(np.all(np.isfinite(v)) for v in telemetry.values()):
                        raise ValueError('Non-finite reset telemetry')
                    fraction = telemetry['env/reset_episode_fraction'][-1]
                    if (arm == 'root' and fraction != 0) or (arm == 'states' and not .4 < fraction < .6):
                        raise ValueError('Realized reset mixture does not match the requested experiment')
                    metadata = {'wall_seconds': seconds, 'telemetry': telemetry, 'checkpoints': [
                        {'path': str(p), 'sha256': digest(p), 'config_sha256': digest(p.parent / 'config.ini'),
                         'steps': int(p.stem), 'train_seconds': max(0, min(seconds, p.stat().st_mtime - started))}
                        for p in checkpoints]}
                    dump(meta_path, metadata)
                metadata = json.loads(meta_path.read_text())
                for index, ck in enumerate(metadata['checkpoints']):
                    if digest(ck['path']) != ck['sha256'] or digest(Path(ck['path']).parent / 'config.ini') != ck['config_sha256']:
                        raise ValueError('Trained checkpoint changed')
                    evaluation = evaluate(args, ck['path'], contract['panel'], out / 'evaluations' / opts['base.run_id'] / str(ck['steps']))
                    results.append({'seed': seed, 'arm': arm, 'steps': ck['steps'], 'score': evaluation['score'],
                                    'train_seconds': ck['train_seconds'], 'checkpoint': ck['path'],
                                    'final': index == len(metadata['checkpoints']) - 1})
                    summarize(out, parent['score'], results, bank)
                    print(f"Completed {opts['base.run_id']} {ck['steps']}: held-out score {evaluation['score']:.3f}", flush=True)
                verify_files(manifest['files'])
        dump(out / 'complete.json', {'complete': True, 'finished_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())})


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=['prepare', 'run'])
    p.add_argument('--out', required=True)
    p.add_argument('--source-run', default='runs/pokemon_event_qd_gen2_20260913')
    p.add_argument('--collector', default='./build/pokemon/collect_states')
    p.add_argument('--trainer', default='./build/pokemon/puffer_states')
    p.add_argument('--evaluator', default='./build/pokemon/pokemon_states')
    p.add_argument('--benchmark', default='./build/pokemon/benchmark_state_bank')
    p.add_argument('--collection-games', type=int, default=8192)
    p.add_argument('--collection-seed', type=int, default=76101)
    p.add_argument('--train-steps', type=int, default=20_000_000)
    p.add_argument('--seeds', type=int, nargs='+', default=[6101, 6102])
    p.add_argument('--eval-seeds', type=int, nargs='+', default=[17101, 17102])
    p.add_argument('--games', type=int, default=64)
    return p


if __name__ == '__main__':
    p = parser()
    args = p.parse_args()
    if not 1 <= args.collection_games <= 50000 or args.train_steps < 1048576 or args.games < 2 or args.games % 2:
        p.error('Invalid collection/training/evaluation budget')
    if len(set(args.seeds)) != len(args.seeds) or len(set(args.eval_seeds)) != len(args.eval_seeds):
        p.error('Seeds must be unique')
    if set(args.seeds) & set(args.eval_seeds) or args.collection_seed in args.seeds + args.eval_seeds:
        p.error('Collection, training and evaluation seeds must be separate')
    run(args)
