#!/usr/bin/env python3
"""Giant run config: the 2B long run's config (final-sweep trial 14 settings) from its best checkpoint (1875M), on the giant
pool (build_pool.py) with 30% of learner seats drafting freely (env.pool_free_fraction: free-pick), 25% of games vs
LeagueV4, 4B steps, a checkpoint about every 250M. Usage (box repo root): make_config.py"""
import glob, re
L = 'reports/pokemon_experiments/20261026_long2b'; G = 'reports/pokemon_experiments/20261027_giant'; V4 = 'leagues/pokemon/league_v4_2b1875'
ck = glob.glob(f'{L}/checkpoints/pokemon/*/0000001875640320.bin')[0]
s = open(f'{L}/pokemon.ini').read()
s = s[s.index('[base]'):]
over = {'base': {'checkpoint_dir': f'{G}/checkpoints', 'log_dir': f'{G}/logs', 'checkpoint_interval': '954', 'load_model_path': ck},
        'selfplay': {'initial_opponents': f'/workspace/PufferLib/{V4}/opponents.txt'},
        'env': {'learner_team_pool': f'{G}/pool.ini', 'native_league': f'{V4}/league.ini'},
        'train': {'total_timesteps': '4000000000'}}
out, sec, added = [], None, False
for line in s.splitlines(True):
    m = re.match(r'\[(.+)\]', line.strip())
    if m: sec = m.group(1)
    k = line.split('=')[0].strip() if '=' in line else None
    if k and k in over.get(sec, {}): line = f'{k} = {over[sec].pop(k)}\n'
    out.append(line)
    if sec == 'env' and k == 'learner_team_pool' and not added:
        out.append('pool_free_fraction = 0.3\n'); added = True
assert added and not any(over[x] for x in over), over
head = ('# Giant run (2026-10-27): 2B long run 1875M -> +4B steps on every Metamon gen1ou team set (exact sets, sampled from all\n'
        '# seven) plus ours, 30% free-pick drafting, 25% of games vs LeagueV4. Held out: A (hl_05_26), B (modern_replays_v2),\n'
        '# C (gl_05_26), our turf. Notes: reports/pokemon_experiments/20261027_giant/README.md.\n')
open(f'{G}/pokemon.ini', 'w').write(head + ''.join(out))
print('ok')
