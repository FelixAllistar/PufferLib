#!/usr/bin/env python3
"""Mirror-test team set "our turf" (30 teams with exact sets), the counterpart of the held-out sets: our 18 training teams
with the sets our 1B checkpoint drafts (export_drafted_teams.py), Princess Klitty's two teams, and 10 random Smogon OU
teams (catalog sets; reports/pokemon_experiments/20261023_next/smogon_pool, banks 0-9), new to Metamon's human data.
Usage (box repo root): build_our_turf.py OUT"""
import json, re, shutil, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
for p in (ROOT / 'reports/pokemon_benchmarks/our_teams_mm1B_sets').glob('*.paste.txt'):
    shutil.copy(p, out / ('ours_' + p.name))
for p in (ROOT / 'reports/pokemon_experiments/20261026_metamon_teams_1b').glob('klitty_*.paste.txt'):
    shutil.copy(p, out / p.name)
sets = {s['id']: s for s in json.loads((ROOT / 'ocean/pokemon/data/catalog.json').read_text())['sets']}
teams = re.findall(r'team = ([0-9,]+)', (ROOT / 'reports/pokemon_experiments/20261023_next/smogon_pool/pool.ini').read_text())
for i, team in enumerate(teams[:10]):
    mons = [sets[int(x)] for x in team.split(',')]
    (out / f'smogon_{i:03d}.paste.txt').write_text('\n\n'.join(m['species'] + ''.join(f'\n- {mv}' for mv in m['moves']) for m in mons) + '\n')
print(len(list(out.glob('*.paste.txt'))), 'teams in', out)
