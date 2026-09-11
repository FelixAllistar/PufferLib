#!/usr/bin/env python3
"""Reallocate existing members from a verified matrix; never promote or prune."""
import argparse
import configparser
import csv
import datetime
from pathlib import Path
import shutil
from policy_identity import identity, aggregated_weights


def repair(league, manifest, meta, apply=False):
    league = Path(league)
    config = configparser.ConfigParser(interpolation=None)
    config.read(league/'league.ini')
    weights = aggregated_weights(manifest, meta)
    with open(manifest) as stream:
        by_identity = {identity(r['checkpoint']): weights[r['policy']]
                       for r in csv.DictReader(stream, delimiter='\t')}
    rows = []
    for section in config.sections():
        if not section.startswith('policy.'): continue
        if not config.getboolean(section, 'enabled', fallback=True):
            raise ValueError('Resolve disabled league members before repair')
        source = config[section]['path'].strip("'\"")
        key = identity(source)
        if key not in by_identity: raise ValueError(f'Unevaluated league member: {source}')
        signal = by_identity[key]
        old_role = config[section].get('role', 'exploration').strip("'\"")
        role = 'meta' if signal >= .01 else ('diversity' if old_role == 'diversity' else 'exploration')
        rows.append(dict(section=section, policy=section[7:], source=source, signal=signal,
                         role=role, old=float(config[section]['train_weight'])))
    shares = {'meta':.7, 'diversity':.25, 'exploration':.05}
    active_share = sum(shares[r] for r in {x['role'] for x in rows})
    for row in rows:
        cohort = [r for r in rows if r['role']==row['role']]
        relative = row['signal']/sum(r['signal'] for r in cohort) if row['role']=='meta' else 1/len(cohort)
        row['weight'] = shares[row['role']]/active_share*relative
        print(f'{row["policy"]}: {row["old"]:.6f} -> {row["weight"]:.6f} ({row["role"]})')
    if not apply: return rows
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    backup = league.parent/(league.name+'_archive')/('weight_repair_'+stamp)
    shutil.copytree(league, backup)
    for row in rows:
        config[row['section']]['train_weight'] = f'{row["weight"]:.12f}'
        config[row['section']]['role'] = row['role']
    temporary = league/'.league.repair.ini'
    with temporary.open('w') as stream: config.write(stream)
    temporary.replace(league/'league.ini')
    temporary = league/'.manifest.repair.tsv'
    with temporary.open('w') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(['policy','role','base_weight','source'])
        writer.writerows((r['policy'],r['role'],f'{r["weight"]:.12f}',r['source']) for r in rows)
    temporary.replace(league/'manifest.tsv')
    print(f'Repaired weights only; members, learner and training config unchanged. Backup: {backup}')
    return rows


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('--league', required=True)
    p.add_argument('--manifest', required=True)
    p.add_argument('--meta', required=True)
    p.add_argument('--apply', action='store_true')
    a=p.parse_args()
    repair(a.league,a.manifest,a.meta,a.apply)
