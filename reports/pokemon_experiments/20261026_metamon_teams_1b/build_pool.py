#!/usr/bin/env python3
"""Team pool for the Metamon-teams run: Metamon's 28 competitive gen1ou teams and Princess Klitty's team with their
exact sets (env "sets:" teams, the file's first Pokemon leads), plus our 18 teams with species fixed (the policy drafts
the moves): the 13 league teams, the 4 OMNI-2 free-pick teams and FP-3's late team. Usage: build_pool.py OUT"""
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
KLITTY = """Alakazam
- Psychic
- Seismic Toss
- Recover
- Thunder Wave

Chansey
- Seismic Toss
- Ice Beam
- Soft-Boiled
- Thunder Wave

Exeggutor
- Psychic
- Mega Drain
- Rest
- Sleep Powder

Cloyster
- Ice Beam
- Clamp
- Rest
- Explosion

Rhydon
- Earthquake
- Body Slam
- Rest
- Substitute

{last}
"""
LAST = {'kang': 'Kangaskhan\n- Earthquake\n- Body Slam\n- Counter\n- Hyper Beam',
        'jynx': 'Jynx\n- Ice Beam\n- Psychic\n- Lovely Kiss\n- Rest'}
banks = []  # (name, team, lead, note)
def exact(path, suffix, note):
    arr = list(load_team(path.stem, rules, path)[0])
    mons = [arr[i:i + 5] for i in range(0, 30, 5)]
    banks.append(('_'.join(code(m[0]) for m in mons) + '.' + suffix,
                  'sets:' + '/'.join(','.join(map(str, m)) for m in mons), str(mons[0][0]), note))
comp = ROOT / 'reports/pokemon_benchmarks/metamon_competitive_gen1ou'
for path in sorted(comp.glob('*.gen1ou_team')):
    exact(path, 'mm' + path.stem.split('_')[1], f'Metamon competitive gen1ou {path.stem} (exact sets)')
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
for last in ('kang', 'jynx'):
    p = out / f'klitty_{last}.paste.txt'; p.write_text(KLITTY.format(last=LAST[last]))
    exact(p, 'klitty', f'Princess Klitty (Smogon) off-meta counter, {last} version (exact sets)')
old = (ROOT / 'reports/pokemon_experiments/20261025_omni2_sweep/pool.ini').read_text()
for name, team, lead in re.findall(r'\[bank\.\d+\]\s*name = (\S+)\s*team = (\S+)\s*lead = (\S+)', old):
    if not name.endswith('.klitty'): banks.append((name, team, lead, 'our team (species fixed, the policy drafts the moves)'))
banks.append(('zam_cha_egg_jynx_lap_lax.s1', 'species:65,113,103,124,131,143', 'None', 'FP-3 late team (species fixed)'))
lines = ['# Metamon-teams run pool (2026-10-26): build_pool.py', '[native]', f'banks = {len(banks)}', '']
for i, (name, team, lead, note) in enumerate(banks):
    lines += [f'# {note}', f'[bank.{i}]', f'name = {name}', f'team = {team}', f'lead = {lead}', '']
(out / 'pool.ini').write_text('\n'.join(lines))
print(len(banks), 'teams;', ', '.join(b[0] for b in banks[:3]), '...')
