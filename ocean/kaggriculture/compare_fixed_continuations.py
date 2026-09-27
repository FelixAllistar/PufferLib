#!/usr/bin/env python3
"""Four bounded continuation ablations, with immutable parent/config provenance.

Uses the one active INI at preparation time, then freezes CLI values in JSON.
Does not edit configs, promote leagues, or submit policies. Training is displayed
in the invoking terminal; each command, log, result and failure is also retained.
"""
import argparse
import configparser
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SECTIONS = ('base', 'policy', 'torch', 'vec', 'selfplay', 'env', 'train')


def cli_value(value):
    return str(value).strip().strip("'\"")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def path_at_root(path):
    path = Path(path)
    return path if path.is_absolute() else ROOT / path


def variants(base):
    lr = float(base['train.learning_rate'])
    ratio = float(base['train.min_lr_ratio'])
    epochs = int(base['train.total_timesteps']) // (
        int(base['vec.total_agents']) * int(base['train.horizon']))
    if epochs < 1 or base['train.anneal_lr'] != '1':
        raise ValueError('Baseline must use cosine annealing for at least one epoch')
    # Match the discrete average LR of native t=0..epochs-1 cosine annealing.
    constant = sum(lr * (ratio + .5 * (1-ratio) * (1 + math.cos(math.pi*t/epochs)))
                   for t in range(epochs)) / epochs
    return [
        ('repeat99', {}),
        ('no_emag', {'train.emag_kl_coef': '0'}),
        ('low_entropy', {'train.emag_kl_coef': '0', 'train.ent_coef': '0.0001'}),
        ('flat_lr', {'train.emag_kl_coef': '0', 'train.ent_coef': '0.0001',
                     'train.anneal_lr': '0', 'train.learning_rate': repr(constant)}),
    ]


def prepare(out, manifest):
    if out.exists():
        raise ValueError(f'Refusing to replace existing study: {out}')
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read([ROOT / 'config/default.ini', ROOT / 'config/kaggriculture.ini'])
    base = {f'{s}.{k}': cli_value(v) for s in SECTIONS if config.has_section(s)
            for k, v in config.items(s)}
    parent = base['base.load_model_path']
    if parent in ('None', 'latest', ''):
        raise ValueError('Pin an explicit parent in the active config first')
    if base['env.macro_executor_version'] != '0' or base['env.observation_version'] != '1':
        raise ValueError('This study requires executor0 / obs1')
    paths = [path_at_root(parent), path_at_root(base['selfplay.opponent_league'])]
    magnet = base['selfplay.magnet_path']
    if magnet == 'None':
        raise ValueError('Pin the parent EMA reference explicitly too')
    paths.append(path_at_root(magnet))
    for suffix, expected in (('.obs_version', '1'), ('.executor_version', '0')):
        side = path_at_root(parent + suffix)
        if side.read_text().strip() != expected:
            raise ValueError(f'Parent contract mismatch: {side}')
        paths.append(side)
    league = configparser.ConfigParser(interpolation=None)
    league.read(paths[1])
    for s in league.sections():
        if s.startswith('policy.') and league.getint(s, 'enabled', fallback=1):
            paths.append(path_at_root(cli_value(league[s]['path'])))
    with open(manifest, newline='') as f:
        opponents = list(csv.DictReader(f, delimiter='\t'))
    if len(opponents) < 2:
        raise ValueError('Need a fixed comparison panel')
    for row in opponents:
        p = path_at_root(row['checkpoint'])
        paths.append(p)
        paths.extend(q for suffix in ('.obs_version', '.executor_version')
                     if (q := Path(str(p) + suffix)).exists())
    paths.extend([ROOT / 'puffer', ROOT / 'ocean/kaggriculture/eval_observation_versions.py'])
    hashes = {str(p): digest(p) for p in paths}
    plan = dict(format='fixed_continuations_v1', base=base, hashes=hashes,
                opponents=opponents, trials=[dict(label=n, overrides=v) for n,v in variants(base)],
                eval_games=100, eval_seed=10843)
    out.mkdir(parents=True)
    (out / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    print(f'Prepared {len(plan["trials"])} independent {base["train.total_timesteps"]}-step branches')
    print(f'Every branch loads {parent}; later config edits cannot change this study.')


def verify(plan):
    for path, expected in plan['hashes'].items():
        if digest(path) != expected:
            raise ValueError(f'Pinned input changed: {path}; stopping rather than mixing experiments')


def command(base, label, overrides):
    values = dict(base, **overrides)
    values.update({'base.run_id': label, 'base.result_fd': '0', 'base.load_enemy_model_path': 'None'})
    if values['base.load_model_path'] in ('latest', 'None', ''):
        raise ValueError('Explicit parent required')
    return ['./puffer', 'train', 'kaggriculture'] + [f'{k}={v}' for k,v in values.items()]


def execute(cmd, log):
    log.with_suffix('.command.json').write_text(json.dumps(cmd, indent=2) + '\n')
    with log.open('wb') as f:
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            while data := proc.stdout.read1(16384):
                f.write(data); f.flush()
                sys.stdout.buffer.write(data); sys.stdout.buffer.flush()
            code = proc.wait()
        except BaseException:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait()
            raise
    if code:
        raise RuntimeError(f'Command failed ({code}); see {log}')


def evaluate(out, plan, label, checkpoint):
    manifest = out / f'{label}_manifest.tsv'
    with manifest.open('w', newline='') as f:
        w = csv.writer(f, delimiter='\t')
        w.writerow(['id', 'policy', 'checkpoint'])
        w.writerow([0, label, checkpoint])
        for i, row in enumerate(plan['opponents'], 1):
            w.writerow([i, row['policy'], row['checkpoint']])
    records = []
    for deterministic in (1, 0):
        prefix = out / f'{label}_d{deterministic}'
        values = dict(plan['base'])
        values.update({'base.run_id': f'{label}_d{deterministic}',
            'base.load_model_path': 'None', 'base.load_enemy_model_path': 'None',
            'base.result_fd': '0', 'base.eval_deterministic': str(deterministic),
            'base.eval_agents': '64', 'base.seed': str(plan['eval_seed']),
            'env.seed': str(plan['eval_seed']), 'train.horizon': '8',
            'train.minibatch_size': '64'})
        cmd = [sys.executable, 'ocean/kaggriculture/eval_observation_versions.py', 'matrix',
               '--manifest', str(manifest), '--focal-count', '1', '--games', str(plan['eval_games']),
               '--output', str(prefix) + '.tsv', '--'] + [f'{k}={v}' for k,v in values.items()]
        execute(cmd, Path(str(prefix) + '.log'))
        with open(str(prefix) + '.tsv', newline='') as f:
            rows = list(csv.reader(f, delimiter='\t'))
        if len(rows) != len(plan['opponents']) or any(int(r[0]) != 0 for r in rows):
            raise ValueError('Incomplete focal evaluation')
        record = dict(deterministic=deterministic, mean_score=sum(float(r[2]) for r in rows)/len(rows),
                      mean_money=sum(float(r[4]) for r in rows)/len(rows), matches=rows)
        records.append(record)
        print(f'RESULT {label} d={deterministic} score={record["mean_score"]:.4f} '
              f'money={record["mean_money"]:.1f}', flush=True)
    return records


def run(out, dry_run=False):
    plan = json.loads((out / 'plan.json').read_text())
    verify(plan)
    for trial in plan['trials']:
        label = f'{out.name}_{trial["label"]}'
        cmd = command(plan['base'], label, trial['overrides'])
        if dry_run:
            values = dict(a.split('=',1) for a in cmd[3:])
            print(json.dumps({k:values[k] for k in ('base.run_id','base.load_model_path',
                'train.total_timesteps','train.learning_rate','train.ent_coef',
                'train.emag_kl_coef','train.anneal_lr','selfplay.magnet_path')}))
            continue
        result_file = out / f'{label}.result.json'
        if result_file.exists():
            if json.loads(result_file.read_text()).get('status') == 'complete':
                continue
            raise ValueError(f'Previous incomplete attempt needs review: {result_file}')
        ckpt_dir = ROOT / plan['base']['base.checkpoint_dir'] / 'kaggriculture' / label
        if ckpt_dir.exists():
            raise ValueError(f'Refusing to overwrite existing run: {ckpt_dir}')
        verify(plan)
        record = dict(status='running', label=label, parent=plan['base']['base.load_model_path'],
                      overrides=trial['overrides'])
        result_file.write_text(json.dumps(record, indent=2) + '\n')
        print(f'START {label}: independent continuation from pinned parent', flush=True)
        try:
            execute(cmd, out / f'{label}.train.log')
            batch = int(plan['base']['vec.total_agents']) * int(plan['base']['train.horizon'])
            steps = int(plan['base']['train.total_timesteps']) // batch * batch
            checkpoint = ckpt_dir / f'{steps:016d}.bin'
            if not checkpoint.is_file():
                raise ValueError(f'Expected final checkpoint missing: {checkpoint}')
            verify(plan)
            record.update(checkpoint=str(checkpoint), evaluations=evaluate(out, plan, label, checkpoint),
                          status='complete')
        except BaseException as e:
            record.update(status='failed', error=str(e))
            raise
        finally:
            result_file.write_text(json.dumps(record, indent=2) + '\n')
    print('DRY RUN COMPLETE' if dry_run else f'COMPLETE: {out}; no promotion/submission', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if args.prepare:
        if args.manifest is None:
            parser.error('--prepare needs --manifest')
        prepare(args.output.resolve(), args.manifest.resolve())
    else:
        run(args.output.resolve(), args.dry_run)
