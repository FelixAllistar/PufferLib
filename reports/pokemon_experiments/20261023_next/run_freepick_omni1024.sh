#!/usr/bin/env bash
# Free-pick from OMNI-1024 (2026-10-25, new box 57608917): the Kirby-loop clone. Free-pick warm-started from
# OMNI-1024's 2B checkpoint (1024x2), no forced cores or move pairs (FP-2's best schedule), opponents random
# 0.3 -> 0 by 20%, 81.5% self-play (31.25% league games), 1B, scored every 125M.
cd /workspace/PufferLib
S=none
R=reports/pokemon_experiments/20261023_next/freepick_omni1024
INIT=$(ls reports/pokemon_experiments/20261023_next/omni_master_1024/checkpoints/pokemon/*/0000001999896576.bin)  # config.ini is beside it
case $S in
  fast) CUR="--env.force_core_prob=0.75 --env.force_core_until=0.5 --env.force_move_pairs_prob=0.75 --env.force_move_pairs_until=0.5 --env.expert_random_start=0.3 --env.expert_random_until=0.2";;
  none) CUR="--env.force_core_prob=0 --env.force_move_pairs_prob=0 --env.expert_random_start=0.3 --env.expert_random_until=0.2";;
  slow) CUR="--env.force_core_prob=0.75 --env.force_core_until=0.9 --env.force_move_pairs_prob=0.75 --env.force_move_pairs_until=0.9 --env.expert_random_start=0.3 --env.expert_random_until=0.4";;
  *) echo "strategy must be fast, none or slow"; exit 1;;
esac
mkdir -p $R
tmux new -d -s freepick_omni1024_eval -c /workspace/PufferLib "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R \
  --base.load_model_path=$INIT --train.total_timesteps=1000000000 --base.checkpoint_interval=477 \
  --env.expert_fraction=0.3125 $CUR --policy.hidden_size=1024
echo FREEPICK_OMNI1024_DONE
