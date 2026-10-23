#!/usr/bin/env bash
# Free-pick from the Smogon-pool OMNI (2026-10-23, B1 box), 81.5% self-play (31.25% league games), 1B, scored
# every 125M. Three annealing strategies (Usage: run_freepick_smogon.sh fast|none|slow):
#   fast: forced move pairs and forced species cores 0.75 -> 0 by half, opponents random 0.3 -> 0 by 20%
#         (same as freepick_omni)
#   none: no forced pairs or cores; opponents random 0.3 -> 0 by 20%
#   slow: forced pairs and cores 0.75 -> 0 by 90%, opponents random 0.3 -> 0 by 40%
cd /workspace/PufferLib
S=$1
R=reports/pokemon_experiments/20261023_next/freepick_smogon_$S
INIT=$(ls reports/pokemon_experiments/20261023_next/omni_smogon_256/checkpoints/pokemon/*/[0-9]*.bin | tail -1)  # config.ini is beside it
case $S in
  fast) CUR="--env.force_core_prob=0.75 --env.force_core_until=0.5 --env.force_move_pairs_prob=0.75 --env.force_move_pairs_until=0.5 --env.expert_random_start=0.3 --env.expert_random_until=0.2";;
  none) CUR="--env.force_core_prob=0 --env.force_move_pairs_prob=0 --env.expert_random_start=0.3 --env.expert_random_until=0.2";;
  slow) CUR="--env.force_core_prob=0.75 --env.force_core_until=0.9 --env.force_move_pairs_prob=0.75 --env.force_move_pairs_until=0.9 --env.expert_random_start=0.3 --env.expert_random_until=0.4";;
  *) echo "strategy must be fast, none or slow"; exit 1;;
esac
mkdir -p $R
tmux new -d -s freepick_smogon_${S}_eval -c /workspace/PufferLib "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R \
  --base.load_model_path=$INIT --train.total_timesteps=1000000000 --base.checkpoint_interval=477 \
  --env.expert_fraction=0.3125 $CUR
echo FREEPICK_SMOGON_${S}_DONE
