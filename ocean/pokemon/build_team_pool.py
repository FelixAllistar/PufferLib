#!/usr/bin/env python3
"""Write a learner team pool (env.learner_team_pool) of random Smogon Gen 1 OU teams with fixed movesets.

Each team takes 6 distinct species uniformly from the species that have a catalog set from Smogon OU
(data/catalog.json, source_format "ou"), and each Pokemon one of that species' OU sets uniformly. Teams are
catalog-set controls ("id,id,...", first set leads), so species and moves are both fixed. Also writes an
eval team list (eval_teams.sh format): named reference teams with OU sets plus the first pool teams.
Usage: build_team_pool.py OUT_DIR [--teams 256] [--seed 20261023]
"""
import argparse, json, random
from pathlib import Path

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('out', type=Path)
parser.add_argument('--teams', type=int, default=256)
parser.add_argument('--seed', type=int, default=20261023)
parser.add_argument('--eval-pool-teams', type=int, default=4)
args = parser.parse_args()

sets = [s for s in json.loads((HERE / 'data/catalog.json').read_text())['sets'] if s['source_format'] == 'ou']
by_species = {}
for s in sets: by_species.setdefault(s['species'], []).append(s['id'])
species = sorted(by_species)
rng = random.Random(args.seed)
teams = []
while len(teams) < args.teams:
    team = ','.join(str(rng.choice(by_species[sp])) for sp in rng.sample(species, 6))
    if team not in teams: teams.append(team)

args.out.mkdir(parents=True, exist_ok=True)
lines = [f'# {len(teams)} random Smogon OU teams from {len(species)} species / {len(sets)} OU catalog sets, seed {args.seed}',
         '[native]', f'banks = {len(teams)}', '']
for i, team in enumerate(teams):
    lines += [f'[bank.{i}]', f'name = smogon_{i:03d}', f'team = {team}', 'lead = None', '']
(args.out / 'pool.ini').write_text('\n'.join(lines))

first = lambda name: by_species[name][0]
reference = {  # recognisable teams, first listed OU set per species
    'freepick_omni_team': ['Alakazam', 'Chansey', 'Lapras', 'Rhydon', 'Starmie', 'Tauros'],
    'gar_egg_zap_ou': ['Gengar', 'Exeggutor', 'Zapdos', 'Chansey', 'Snorlax', 'Tauros'],
    'zam_egg_cloy_don_ou': ['Alakazam', 'Exeggutor', 'Rhydon', 'Cloyster', 'Chansey', 'Tauros'],
}
evals = ['# name team lead: reference teams with Smogon OU sets, then the first pool teams']
evals += [f'{name} {",".join(str(first(sp)) for sp in members)} None' for name, members in reference.items()]
evals += [f'smogon_{i:03d} {teams[i]} None' for i in range(args.eval_pool_teams)]
(args.out / 'eval_teams.txt').write_text('\n'.join(evals) + '\n')
print(f'{len(teams)} teams from {len(species)} species -> {args.out}/pool.ini, eval list {args.out}/eval_teams.txt')
