#!/usr/bin/env python3
"""Final-sweep team pool: the Metamon-teams run's 48 teams (Metamon's 28 competitive teams and Princess Klitty's with
exact sets, our 18 with species fixed) plus ladder teams with exact sets sampled from Metamon's hl_05_26 and
modern_replays_v2 sets (sample_teams.py dirs; disjoint from both held-out evaluation samples).
Usage (box repo root): build_pool.py OUT LADDER_DIR..."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'ocean/pokemon/benchmarks'))
from evaluate_private import load_team
from private_view import PublicMechanics

CODES = {'Alakazam': 'zam', 'Chansey': 'cha', 'Exeggutor': 'egg', 'Lapras': 'lap', 'Rhydon': 'don', 'Golem': 'gol',
         'Starmie': 'mie', 'Tauros': 'tau', 'Zapdos': 'zap', 'Gengar': 'gar', 'Snorlax': 'lax', 'Cloyster': 'cloy',
         'Jynx': 'jynx', 'Kangaskhan': 'kang'}
rules = PublicMechanics()
names = {p['id']: p['name'] for p in rules.data['species']}
code = lambda sid: CODES.get(names[sid], re.sub(r'[^a-z]', '', names[sid].lower())[:4])
out, ladders = Path(sys.argv[1]), sys.argv[2:]
base = (ROOT / 'reports/pokemon_experiments/20261026_metamon_teams_1b/pool.ini').read_text()
banks = [(n, t, l, 'Metamon-teams run pool') for n, t, l in re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', base)]
seen = {t for _, t, _, _ in banks}
for d in ladders:
    for p in sorted(Path(d).glob('*.gen1ou_team')):
        mons = [list(load_team(p.stem, rules, p)[0])[i:i + 5] for i in range(0, 30, 5)]
        team = 'sets:' + '/'.join(','.join(map(str, m)) for m in mons)
        if team in seen: continue
        seen.add(team)
        banks.append(('_'.join(code(m[0]) for m in mons) + f'.{Path(d).name.split("_")[1]}{p.stem[1:]}', team, str(mons[0][0]),
                      f'ladder team {Path(d).name}/{p.name} (exact sets)'))
lines = ['# Final 1024 sweep pool (2026-10-26): build_pool.py', '[native]', f'banks = {len(banks)}', '']
for i, (name, team, lead, note) in enumerate(banks):
    lines += [f'# {note}', f'[bank.{i}]', f'name = {name}', f'team = {team}', f'lead = {lead}', '']
out.mkdir(parents=True, exist_ok=True)
(out / 'pool.ini').write_text('\n'.join(lines))
print(len(banks), 'teams')
