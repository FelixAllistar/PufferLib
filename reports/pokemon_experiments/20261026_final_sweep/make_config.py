#!/usr/bin/env python3
"""Final-sweep config from the Metamon-teams 1B run's config (trial 23 settings): warm start from its 1B checkpoint, the
348-team pool, LeagueV3, 150M-step trials, Metamon off. Usage (box repo root): make_config.py"""
import glob, re
M = 'reports/pokemon_experiments/20261026_metamon_teams_1b'; F = 'reports/pokemon_experiments/20261026_final_sweep'
L = 'leagues/pokemon/league_v3_mm1B'
ck = glob.glob(f'{M}/checkpoints/pokemon/*/0000000999817216.bin')[0]
s = open(f'{M}/pokemon.ini').read()
s = s[s.index('[base]'):]
over = {'base': {'checkpoint_dir': f'{F}/checkpoints', 'log_dir': f'{F}/logs', 'checkpoint_interval': '100000', 'load_model_path': ck},
        'selfplay': {'initial_opponents': f'/workspace/PufferLib/{L}/opponents.txt'},
        'env': {'learner_team_pool': f'{F}/pool.ini', 'native_league': f'{L}/league.ini'},
        'train': {'total_timesteps': '150000000'},
        'sweep': {'max_runs': '500', 'metric': 'vs_league_late', 'history_path': f'{F}/sweep.history', 'early_stop_quantile': '0'},
        'sweep.train.total_timesteps': {'min': '150000000', 'max': '150000000'},
        'metamon': {'enabled': '0'}}
out, sec = [], None
for line in s.splitlines(True):
    m = re.match(r'\[(.+)\]', line.strip())
    if m: sec = m.group(1)
    k = line.split('=')[0].strip() if '=' in line else None
    if k and k in over.get(sec, {}): line = f'{k} = {over[sec].pop(k)}\n'
    out.append(line)
assert not any(over[x] for x in over), over
head = ('# Final 1024 sweep (2026-10-26): from the Metamon-teams 1B checkpoint (which beats TaurosV0 59.7% / SyntheticRLV2 68.2%\n'
        '# in the held-out mirror test), 150M steps per trial on 348 teams (Metamon competitive + Princess Klitty + 300 ladder\n'
        '# teams with exact sets, plus our 18; both held-out evaluation samples excluded), 25% of games vs LeagueV3 (the 1B\n'
        '# checkpoint on 16 teams). Swept: LR and its anneal floor, entropy and its floor, magnet; replay pinned to 1 and every\n'
        '# inherited default.ini dimension pinned. Score: vs_league_late. sweep_heldout.py gives every new top-3 trial the\n'
        '# held-out mirror test vs both Metamon models. Notes: reports/pokemon_experiments/20261026_metamon_teams_1b/README.md.\n')
open(f'{F}/pokemon.ini', 'w').write(head + ''.join(out))
print('ok')
