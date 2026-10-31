"""Config for the Gen 1 trainer fine-tune (2026-10-30): the ladder run's resolved config with trainer mode on.

Starts from the ladder-run 2B checkpoint (our best Gen 1 OU model); every game is two in-game trainer parties rebuilt
at level 100 with Showdown-standard stats (trainers_l100.tsv), each side picking up to three after seeing both parties
(Pokemon Stadium's cup format), half mirror matches and half against a trainer within 300 of its level-100 rating.
Pure self-play: no frozen experts, team pools or Metamon evaluations. Cartridge mechanics (PK_CARTRIDGE=1 build).
Run on the box from /workspace/PufferLib: python3 THIS > config/pokemon.ini (the previous one is kept by the caller).
"""
import re

L = 'reports/pokemon_experiments/20261028_ladder'
R = 'reports/pokemon_experiments/20261030_gen1_trainers'
src = open(f'{L}/logs/pokemon/sweep_1793048711881_0014.ini').read()
src = src[src.index('[base]'):]
over = {
    'base': {'checkpoint_dir': f'{R}/run/checkpoints', 'log_dir': f'{R}/run/logs', 'checkpoint_interval': 100,
             'load_model_path': f'{L}/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin'},
    'vec': {'num_policies': 1, 'hist_policy_percent': 0},
    'selfplay': {'enabled': 0},
    'env': {'expert_fraction': 0, 'learner_team_pool': 'None', 'pool_free_fraction': 0,
            'trainer_file': 'ocean/pokemon/cartridge/trainers_l100.tsv',
            'trainer_ratings': 'ocean/pokemon/cartridge/trainer_ratings_l100.txt',
            'trainer_mirror': 0.5, 'trainer_window': 300, 'trainer_perfect': 1, 'trainer_pick': 1,
            'trainer_tournament': 'None', 'trainer_tournament_games': 20,
            'trainer_tournament_sets': 0},
    'train': {'total_timesteps': 1000000000},
    'metamon': {'enabled': 0},
}
out, sec, seen = [], None, {k: set() for k in over}
for line in src.splitlines(keepends=True):
    m = re.match(r'\[(.+)\]', line.strip())
    if m:
        if sec in over:  # keys this section lacked
            out += [f'{k} = {v}\n' for k, v in over[sec].items() if k not in seen[sec]]
        sec = m.group(1)
    key = line.split('=')[0].strip() if '=' in line and not line.lstrip().startswith(('#', ';')) else None
    if sec in over and key in over[sec]:
        line = f'{key} = {over[sec][key]}\n'
        seen[sec].add(key)
    out.append(line)
print('# Gen 1 trainer fine-tune (2026-10-30): ladder-run 2B -> level-100 trainer parties, pick three. '
      'Notes: reports/pokemon_experiments/20261030_gen1_trainers\n' + ''.join(out), end='')
