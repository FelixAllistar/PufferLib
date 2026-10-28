#!/usr/bin/env python3
"""Giant-run pool: the final sweep's 348 teams (Metamon's competitive teams, Princess Klitty, ours, 300 ladder teams) plus
ladder teams with exact sets sampled from all seven Metamon gen1ou sets (make_teams.sh), deduplicated; all held-out
samples (A, B, C, our turf) excluded. Usage (box repo root): build_pool.py"""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'ocean/pokemon/benchmarks'))
from evaluate_private import load_team
from private_view import PublicMechanics
G = ROOT / 'reports/pokemon_experiments/20261027_giant'
rules = PublicMechanics()
names = {p['id']: p['name'] for p in rules.data['species']}
code = lambda sid: re.sub(r'[^a-z]', '', names[sid].lower())[:4]
base = (ROOT / 'reports/pokemon_experiments/20261026_final_sweep/pool.ini').read_text()
banks = [(n, t, l, 'final sweep pool') for n, t, l in re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', base)]
seen = {t for _, t, _, _ in banks}
for d in sorted(G.glob('ladder_*')):
    tag = d.name[len('ladder_'):]
    for p in sorted(d.glob('*.gen1ou_team')):
        mons = [list(load_team(p.stem, rules, p)[0])[i:i + 5] for i in range(0, 30, 5)]
        team = 'sets:' + '/'.join(','.join(map(str, m)) for m in mons)
        if team in seen: continue
        seen.add(team)
        banks.append(('_'.join(code(m[0]) for m in mons) + f'.{tag}{p.stem[1:]}', team, str(mons[0][0]), f'{tag}/{p.name} (exact sets)'))
lines = ['# Giant run pool (2026-10-27): build_pool.py', '[native]', f'banks = {len(banks)}', '']
for i, (name, team, lead, note) in enumerate(banks):
    lines += [f'# {note}', f'[bank.{i}]', f'name = {name}', f'team = {team}', f'lead = {lead}', '']
(G / 'pool.ini').write_text('\n'.join(lines))
print(len(banks), 'teams')
