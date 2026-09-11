#!/usr/bin/env python3
"""Evaluate candidates on the SAME immutable panel and emit generic archive input.

Run from the repository root. Each --candidate/--opponent is LABEL=PATH or
LABEL=random. No 'latest': identities and panel fingerprints must be stable.
This is offline CPU evaluation, not a training or live-league mutation tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


SCHEMA = {
    'species': {'kind': 'distribution', 'size': 149, 'weight': 1},
    'leads': {'kind': 'distribution', 'size': 149, 'weight': 1},
    'types': {'kind': 'distribution', 'size': 32, 'weight': .5},
    'mean_team_hp': {'kind': 'scalar', 'weight': .5},
    'survivors': {'kind': 'scalar', 'weight': .5},
    'duration': {'kind': 'scalar', 'weight': .25},
    'timeout': {'kind': 'scalar', 'weight': .25},
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identities(specs):
    result = {}
    for spec in specs:
        name, sep, path = spec.partition('=')
        if not sep or not name or not path or name in result or path == 'latest':
            raise ValueError('require unique LABEL=CHECKPOINT or LABEL=random; no latest')
        if path == 'random':
            result[name] = {'path': path, 'sha256': 'uniform-legal-random-v1'}
        else:
            p = Path(path).resolve(strict=True)
            config = p.parent/'config.ini'
            result[name] = {'path': str(p), 'sha256': digest(p), 'config_sha256': digest(config)}
    return result


def aggregate(episodes):
    result = {}
    for key, spec in SCHEMA.items():
        if spec['kind'] == 'scalar':
            result[key] = sum(e['descriptors'][key] for e in episodes)/len(episodes)
        else:
            result[key] = [0.0]*spec['size']
            for e in episodes:
                values = e['descriptors'][key]
                if len(values) != spec['size'] or sum(values) <= 0:
                    raise ValueError('invalid profile histogram')
                for i,v in enumerate(values): result[key][i] += v/sum(values)/len(episodes)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate', action='append', required=True)
    p.add_argument('--opponent', action='append', required=True)
    p.add_argument('--games', type=int, default=256)
    p.add_argument('--seed', type=int, default=42)
    p.add_argument('--max-updates', type=int, default=512)
    p.add_argument('--binary', default='./pokemon')
    p.add_argument('--output', required=True)
    args = p.parse_args()
    if args.games < 2 or args.games%2 or args.max_updates < 1 or args.seed < 0:
        p.error('games must be positive and even; seed nonnegative; max-updates positive')
    if Path(args.output).exists(): p.error('output already exists; use a new path')
    candidates, panel = identities(args.candidate), identities(args.opponent)
    contract = {'adapter': 'pokemon-realized-v1', 'binary_sha256': digest(args.binary),
                'catalog_sha256': digest('ocean/pokemon/data/catalog.json'),
                'games': args.games, 'seed': args.seed, 'max_updates': args.max_updates,
                'team_selection': 1, 'sampling': 'stochastic', 'opponents': panel}
    panel_id = hashlib.sha256(json.dumps(contract,sort_keys=True).encode()).hexdigest()
    records = []
    for label, candidate in candidates.items():
        episodes, payoffs = [], {}
        for opponent, identity in panel.items():
            print(f'Evaluating {label} vs {opponent}: {args.games} games',flush=True)
            cmd = [args.binary,'eval',candidate['path'],identity['path'],'--profile-json',
                   f'--games={args.games}',f'--seed={args.seed}',
                   'env.team_selection=1',f'env.max_updates={args.max_updates}']
            run = subprocess.run(cmd, text=True, capture_output=True, check=True)
            rows = [json.loads(line[len('PK_PROFILE '):]) for line in run.stdout.splitlines()
                    if line.startswith('PK_PROFILE ')]
            if len(rows) != args.games: raise ValueError('missing episode profiles')
            payoffs[opponent] = {'score': sum(r['score'] for r in rows)/len(rows), 'games': len(rows)}
            episodes.extend(dict(r,opponent=opponent) for r in rows)
        records.append({'id':label,'identity':candidate,'descriptors':aggregate(episodes),
                        'payoffs':payoffs,'episodes':episodes})
    # Detect weights/config changes during evaluation rather than mixing identities.
    if candidates != identities(args.candidate) or panel != identities(args.opponent) or digest(args.binary) != contract['binary_sha256']:
        raise ValueError('evaluation inputs changed during run; discard results')
    data = {'version':1,'schema_id':'pokemon-realized-v1','schema':SCHEMA,
            'panel_id':panel_id,'evaluation':contract,'records':records}
    with open(args.output,'x') as f: json.dump(data,f,indent=2,allow_nan=False)
    print(f'Wrote {args.output}; no league or training settings changed')


if __name__ == '__main__': main()
