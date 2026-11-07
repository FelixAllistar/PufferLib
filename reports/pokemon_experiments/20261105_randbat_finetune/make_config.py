"""Config for the Gen 1 random-battle fine-tune (2026-11-05): the ladder run's config with trainer mode on.

Starts from the ladder-run 2B checkpoint (checkpoints/pokemon_final/20261028_ladder_2B.bin, our best Gen 1 OU model).
Every game is two different teams drawn from 32,000 Showdown gen1randombattle teams (gen_teams.js) at their own levels,
Showdown stats and full PP (trainer_perfect 2), whole teams, first Pokemon leads, no pick phase. Pure self-play.
Showdown-compatible engine build (the normal ./puffer). Run from /workspace/PufferLib:
python3 THIS [TOTAL_STEPS] [LOAD_PATH] > config/pokemon.ini
"""
import re, sys

L = 'reports/pokemon_experiments/20261028_ladder'
R = 'reports/pokemon_experiments/20261105_randbat_finetune'
steps = sys.argv[1] if len(sys.argv) > 1 else '1000000000'
load = sys.argv[2] if len(sys.argv) > 2 else 'checkpoints/pokemon_final/20261028_ladder_2B.bin'
src = open(f'{L}/pokemon.ini').read()
src = src[src.index('[base]'):]
over = {
    'base': {'checkpoint_dir': f'{R}/run/checkpoints', 'log_dir': f'{R}/run/logs', 'checkpoint_interval': 100,
             'load_model_path': load},
    'vec': {'num_policies': 1, 'hist_policy_percent': 0},
    'selfplay': {'enabled': 0},
    'env': {'expert_fraction': 0, 'learner_team_pool': 'None', 'pool_free_fraction': 0, 'native_league': 'None',
            'match_team_pool': 'None',
            'trainer_file': f'{R}/randbat_teams.tsv', 'trainer_ratings': f'{R}/randbat_ratings.txt',
            'trainer_mirror': 0, 'trainer_window': 1000000, 'trainer_perfect': 2, 'trainer_pick': 0,
            'trainer_tournament': 'None', 'trainer_tournament_games': 20,
            'trainer_tournament_sets': 0, 'trainer_ai_player': 'None', 'trainer_ai_opponents': 'None',
            'trainer_ai_badges': 255, 'trainer_ai_perfect': 1, 'trainer_names': 'None', 'trainer_replay_dir': 'None',
            'trainer_replay_games': 20},
    'train': {'total_timesteps': steps, 'replay_ratio': 1},
    'metamon': {'enabled': 0},
}
out, sec, seen = [], None, {k: set() for k in over}
def flush():
    if sec in over: out.extend(f'{k} = {v}\n' for k, v in over[sec].items() if k not in seen[sec])
for line in src.splitlines(keepends=True):
    m = re.match(r'\[(.+)\]', line.strip())
    if m:
        flush(); sec = m.group(1)
    key = line.split('=')[0].strip() if '=' in line and not line.lstrip().startswith(('#', ';')) else None
    if sec in over and key in over[sec]:
        line = f'{key} = {over[sec][key]}\n'; seen[sec].add(key)
    out.append(line)
flush()
missing = [s for s in over if f'[{s}]' not in src]
for s in missing: out.append(f'\n[{s}]\n' + ''.join(f'{k} = {v}\n' for k, v in over[s].items()))
print('# Gen 1 random-battle fine-tune (2026-11-05): ladder-run 2B -> Showdown gen1randombattle teams. '
      'Notes: reports/pokemon_experiments/20261105_randbat_finetune\n' + ''.join(out), end='')
