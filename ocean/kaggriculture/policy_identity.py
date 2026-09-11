#!/usr/bin/env python3
"""Checkpoint content identity; filenames are provenance, not policy identity."""
import argparse
import csv
import hashlib
from pathlib import Path

from eval_observation_versions import observation_version, executor_version


def digest(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def identity(path):
    version = executor_version(path)
    contract = f':exec{version}' if version else ''
    return f'obs{observation_version(path)}{contract}:{digest(path)}'


def deduplicate(paths, focal=0):
    seen, kept, aliases = {}, [], []
    new_focal = 0
    for index, path in enumerate(paths):
        key = identity(path)
        if key not in seen:
            seen[key] = str(path)
            kept.append(str(path))
            new_focal += int(focal > 0 and index < focal)
        aliases.append((str(path), seen[key], key))
    return kept, new_focal, aliases


def aggregated_weights(manifest, meta):
    with open(manifest) as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    with open(meta) as stream:
        weights = {r['policy']: float(r['weight'])
                   for r in csv.DictReader(stream, delimiter='\t')
                   if not r['policy'].startswith('#')}
    keys = {r['policy']: identity(r['checkpoint']) for r in rows}
    totals = {}
    for name, key in keys.items():
        totals[key] = totals.get(key, 0) + weights.get(name, 0)
    return {name: totals[key] for name, key in keys.items()}


def main():
    parser = argparse.ArgumentParser(__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    dedup = sub.add_parser('dedup')
    dedup.add_argument('--focal-count', type=int, default=0)
    dedup.add_argument('--aliases', required=True)
    dedup.add_argument('paths', nargs='+')
    meta = sub.add_parser('weights')
    meta.add_argument('--manifest', required=True)
    meta.add_argument('--meta', required=True)
    args = parser.parse_args()
    if args.command == 'dedup':
        kept, focal, aliases = deduplicate(args.paths, args.focal_count)
        with open(args.aliases, 'w') as stream:
            writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
            writer.writerow(['path', 'canonical_path', 'identity'])
            writer.writerows(aliases)
        print(focal)
        print('\n'.join(kept))
    else:
        print('policy\tweight')
        for name, weight in aggregated_weights(args.manifest, args.meta).items():
            print(f'{name}\t{weight:.12f}')


if __name__ == '__main__':
    main()
