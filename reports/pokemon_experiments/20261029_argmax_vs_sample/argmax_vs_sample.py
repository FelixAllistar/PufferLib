#!/usr/bin/env python3
"""Argmax vs sampled inference (2026-10-29): the ladder-run 2B model plays itself on the live ladder team (Zapdos,
round-robin winner), one side argmax and the other sampling (base.eval_greedy / base.enemy_greedy, ./puffer match,
8192 games). Rows: mirror sanity (both sample), argmax vs sample, sample vs argmax (should mirror it), both argmax.
Results: argmax_vs_sample.txt."""
import re, subprocess, sys
sys.path.insert(0, 'ocean/pokemon/benchmarks')
from pathlib import Path
from evaluate_private import load_team
from private_view import PublicMechanics
rules = PublicMechanics(); R = Path('reports/pokemon_experiments/20261029_argmax_vs_sample'); R.mkdir(parents=True, exist_ok=True)
CK = 'reports/pokemon_experiments/20261028_ladder/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin'
P = Path('reports/pokemon_experiments/20261028_round_robin/teams/egg1_std_zapdos.paste.txt')
a = list(load_team(P.stem, rules, P)[0]); team = 'sets:' + '/'.join(','.join(map(str, a[i:i + 5])) for i in range(0, 30, 5)); lead = str(a[0])
for label, g, e in [('both sample (sanity)', 0, -1), ('learner argmax vs enemy sample', 1, 0),
                    ('learner sample vs enemy argmax', 0, 1), ('both argmax', 1, 1)]:
    out = subprocess.run(['./puffer', 'match', '--headless', '--base.env_name=pokemon', '--base.eval_episodes=8192',
        f'--base.load_model_path={CK}', f'--base.load_enemy_model_path={CK}', '--policy.hidden_size=1024', '--policy.num_layers=2',
        f'--base.eval_greedy={g}', f'--base.enemy_greedy={e}',
        f'--env.learner_team={team}', f'--env.opponent_team={team}', f'--env.learner_lead={lead}', f'--env.opponent_lead={lead}',
        '--env.learner_team_pool=None', '--env.match_team_pool=None', '--env.native_league=None', '--env.pool_free_fraction=0',
        '--env.expert_fraction=0'], capture_output=True, text=True).stdout
    mode = re.search(r'Match actions: .*', out); m = re.search(r'CUDA_EVAL env=pokemon score=(\S+) .* games=(\d+)', out)
    line = f'{label}\t{100 * float(m.group(1)):.1f}% for the learner\t{m.group(2)} games\t{mode.group(0) if mode else "(both follow eval_greedy)"}' if m else f'{label}\tFAILED'
    print(line, flush=True); open(R / 'argmax_vs_sample.txt', 'a').write(line + '\n')
