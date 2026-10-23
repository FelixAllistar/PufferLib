#!/usr/bin/env bash
# Free-pick warm start (2026-10-23, B1 box). Free-pick v2's settings (drafts its own team, 94% of games vs
# the upgraded 13-bank league, forced-core share 0.75 -> 0 by mid-training), but starting from the B1
# gar_egg_zap 700M policy (61.8 / 58.0, same 256x2 PufferNet) instead of from scratch, so no easy-opponent
# phase. 1B steps. Question: is free-pick's ~40 plateau the from-scratch start, or drafting itself?
# Compare: free-pick v1 best 33.7 / 49.7, v2 best 33.7 / 33.3; B1 gar_egg_zap on its fixed team 61.8 / 58.0.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261023_next/freepick_warm
INIT=$(ls reports/pokemon_experiments/20261022_b1_teams/gar_egg_zap/checkpoints/pokemon/*/0000000700186624.bin)
mkdir -p $R
tmux new -d -s fpwarm_eval "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R \
  --base.load_model_path=$INIT --train.total_timesteps=1000000000 --base.checkpoint_interval=477 \
  --env.force_core_until=0.5
echo FREEPICK_WARM_DONE
