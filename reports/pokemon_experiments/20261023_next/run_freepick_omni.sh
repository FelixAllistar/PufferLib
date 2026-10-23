#!/usr/bin/env bash
# Free-pick from OMNI (2026-10-23, B1 box): free-pick warm-started from OMNI_MASTER's final checkpoint (one
# 256x2 policy that plays all 13 league teams), with the move_pairs settings (forced move pairs 0.75 -> 0 by
# half, forced species cores 0.75 -> 0 by half, opponents random 0.3 -> 0 by 20%) but mostly self-play:
# 31.25% league games (env.expert_fraction 0.3125), so 81.5% of the learner's play is self-play (it has both
# seats in a self-play game); the earlier free-pick runs had 93.75% league games. 1B, scored every 125M.
# Question: starting from a policy that already pilots the meta species, does free-pick keep drafting them?
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261023_next/freepick_omni
INIT=$(ls reports/pokemon_experiments/20261023_next/omni_master_256/checkpoints/pokemon/*/[0-9]*.bin | tail -1)  # config.ini is beside it
mkdir -p $R
tmux new -d -s freepick_omni_eval -c /workspace/PufferLib "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R \
  --base.load_model_path=$INIT --train.total_timesteps=1000000000 --base.checkpoint_interval=477 \
  --env.force_core_until=0.5 \
  --env.force_move_pairs_prob=0.75 --env.force_move_pairs_until=0.5 \
  --env.expert_random_start=0.3 --env.expert_random_until=0.2 --env.expert_fraction=0.3125
echo FREEPICK_OMNI_DONE
