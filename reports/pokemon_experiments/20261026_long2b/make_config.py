#!/usr/bin/env python3
"""Long-run config: final-sweep trial 14's exact config (its saved config.ini, checkpoints every 477 epochs ~ 125M steps; tied best sweep score 0.493, held-out mirror
Kakuna 50.9 / TaurosV0 62.4 / SyntheticRLV2 68.6 at 150M), warm start from the same Metamon-teams 1B checkpoint, run to
2B steps so the LR and entropy anneals stretch over the whole run. Usage (box repo root): make_config.py"""
import re
F = 'reports/pokemon_experiments/20261026_final_sweep'; D = 'reports/pokemon_experiments/20261026_long2b'
s = open(f'{F}/checkpoints/pokemon/sweep_1793048711881_0014/config.ini').read()
over = {'base': {'checkpoint_dir': f'{D}/checkpoints', 'log_dir': f'{D}/logs', 'checkpoint_interval': '477'}, 'train': {'total_timesteps': '2000000000'}}
out, sec = [], None
for line in s.splitlines(True):
    m = re.match(r'\[(.+)\]', line.strip())
    if m: sec = m.group(1)
    k = line.split('=')[0].strip() if '=' in line else None
    if k and k in over.get(sec, {}): line = f'{k} = {over[sec].pop(k)}\n'
    out.append(line)
assert not any(over[x] for x in over), over
head = ('# Long run (2026-10-26): final-sweep trial 14 settings, Metamon-teams 1B checkpoint -> 2B steps, 348-team pool,\n'
        '# 25% of games vs LeagueV3. Notes: reports/pokemon_experiments/20261026_long2b/README.md.\n')
open(f'{D}/pokemon.ini', 'w').write(head + ''.join(out))
print('ok')
