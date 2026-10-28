#!/usr/bin/env python3
"""Size study (2026-10-27): the giant run's recipe (3605-team pool, 30% free-pick drafting, 25% vs LeagueV4, final-sweep
trial 14 training settings) from scratch at six policy sizes, 2B steps each, a checkpoint about every 500M. Only the
learner's width and depth change (and its historical-self opponents, which must match it); LeagueV4's banks stay 1024x2.
Writes SIZE/pokemon.ini for each. Usage (box repo root): make_configs.py"""
import os, re
G = 'reports/pokemon_experiments/20261027_giant'; Z = 'reports/pokemon_experiments/20261027_size'
SIZES = [(512, 2), (512, 3), (1024, 2), (1024, 3), (2048, 2), (2048, 3)]
base = open(f'{G}/pokemon.ini').read()
for h, l in SIZES:
    d = f'{Z}/h{h}x{l}'
    over = {'base': {'checkpoint_dir': f'{d}/checkpoints', 'log_dir': f'{d}/logs', 'checkpoint_interval': '1908',
                     'load_model_path': 'None'},
            'vec': {'hist_policy_hidden_size': str(h), 'hist_policy_num_layers': str(l)},
            'policy': {'hidden_size': str(h), 'num_layers': str(l)},
            'train': {'total_timesteps': '2000000000'}}
    out, sec = [], None
    for line in base.splitlines(True):
        m = re.match(r'\[(.+)\]', line.strip())
        if m: sec = m.group(1)
        k = line.split('=')[0].strip() if '=' in line else None
        if k and k in over.get(sec, {}): line = f'{k} = {over[sec].pop(k)}\n'
        out.append(line)
    assert not any(over[x] for x in over), (h, l, over)
    os.makedirs(d, exist_ok=True)
    head = (f'# Size study {h}x{l} (2026-10-27): giant-run recipe from scratch, 2B steps. Notes: {Z}/README.md\n')
    body = ''.join(out)
    body = body[body.index('[base]'):] if body.startswith('#') else body
    open(f'{d}/pokemon.ini', 'w').write(head + body)
    print(d)
