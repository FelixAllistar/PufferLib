#!/usr/bin/env python3
"""Ladder run config (2026-10-28): the giant run's recipe continued from its 4B checkpoint on the harvested pool
(harvest_pool.py: 4150 unique teams = giant pool + Smogon + former held-out sets + every ladder opponent, ladder teams
weighted 10x), 30% free-pick, 25% vs LeagueV4, 2B steps, a checkpoint about every 250M. Usage: make_config.py"""
import glob, re
G = 'reports/pokemon_experiments/20261027_giant'; L = 'reports/pokemon_experiments/20261028_ladder'
ck = glob.glob(f'{G}/checkpoints/pokemon/*/0000003999793152.bin')[0]
s = open(f'{G}/pokemon.ini').read(); s = s[s.index('[base]'):]
over = {'base': {'checkpoint_dir': f'{L}/checkpoints', 'log_dir': f'{L}/logs', 'load_model_path': ck},
        'env': {'learner_team_pool': f'{L}/pool.ini'}, 'train': {'total_timesteps': '2000000000'}}
out, sec = [], None
for line in s.splitlines(True):
    m = re.match(r'\[(.+)\]', line.strip())
    if m: sec = m.group(1)
    k = line.split('=')[0].strip() if '=' in line else None
    if k and k in over.get(sec, {}): line = f'{k} = {over[sec].pop(k)}\n'
    out.append(line)
assert not any(over[x] for x in over), over
open(f'{L}/pokemon.ini', 'w').write('# Ladder run (2026-10-28): giant 4B -> +2B on the harvested 4150-team pool. Notes: README.md\n' + ''.join(out))
print('ok')
