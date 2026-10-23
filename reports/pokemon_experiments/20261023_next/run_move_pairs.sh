#!/usr/bin/env bash
# Move + team exploration (2026-10-23, B1 box). Free-pick warm-started from B1 gar_egg_zap 700M, like
# freepick_warm, plus:
#   - forced move pairs: in 75% of learner games each drafted Pokemon is dealt 2 random legal moves and the
#     policy picks the other 2 (env.force_move_pairs_prob), annealed to 0 by half of training;
#   - forced species cores as before (0.75 of games, all 149 species, annealed to 0 by half);
#   - slight opponent randomness: league opponents act randomly with probability 0.3, falling to 0 by 20%.
# Drafted Pokemon must now fill all four move slots. 1B steps, scored every 125M; logs smogon_set_rate_free.
# Compare: freepick_warm (15% exact Smogon sets, ~40 vs Metamon); B0 gar picks Smogon sets 83% of the time.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261023_next/move_pairs
INIT=$(ls reports/pokemon_experiments/20261022_b1_teams/gar_egg_zap/checkpoints/pokemon/*/0000000700186624.bin)  # needs config.ini beside it
mkdir -p $R
tmux new -d -s move_pairs_eval -c /workspace/PufferLib "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R \
  --base.load_model_path=$INIT --train.total_timesteps=1000000000 --base.checkpoint_interval=477 \
  --env.force_core_until=0.5 \
  --env.force_move_pairs_prob=0.75 --env.force_move_pairs_until=0.5 \
  --env.expert_random_start=0.3 --env.expert_random_until=0.2
echo MOVE_PAIRS_DONE
