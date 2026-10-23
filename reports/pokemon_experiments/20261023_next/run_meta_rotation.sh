#!/usr/bin/env bash
# Meta-species rotation (2026-10-23, window box): free-pick warm-started from B1 gar_egg_zap 700M, like
# freepick_warm, but the forced 3-species cores come from 15 standard Gen 1 OU species instead of all 149,
# and the forced share stays 0.75 for the whole run (no annealing). The learner keeps piloting Chansey,
# Snorlax, Exeggutor, Tauros and the rest, so it has to learn their moves; the other 3 species and all moves
# are its own picks, and 25% of games are fully free drafts. 1B steps, scored every 125M.
# Pool: Alakazam 65, Golem 76, Slowbro 80, Cloyster 91, Gengar 94, Exeggutor 103, Rhydon 112, Chansey 113,
# Starmie 121, Jynx 124, Tauros 128, Lapras 131, Jolteon 135, Snorlax 143, Zapdos 145.
# Compare: freepick_warm (all-species cores annealed off), free-pick v1/v2, B1 gar 61.8 / 58.0.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261023_next/meta_rotation
INIT=$(ls reports/pokemon_experiments/20261022_b1_teams/gar_egg_zap/checkpoints/pokemon/*/0000000700186624.bin)
mkdir -p $R
tmux new -d -s meta_rot_eval -c /workspace/PufferLib "bash reports/pokemon_experiments/20261023_freepick_v2/eval_dir.sh $R; exec bash"
bash reports/pokemon_experiments/20261022_freepick_league/run_freepick.sh $R \
  --base.load_model_path=$INIT --train.total_timesteps=1000000000 --base.checkpoint_interval=477 \
  --env.core_pool=65,76,80,91,94,103,112,113,121,124,128,131,135,143,145 --env.force_core_prob=0.75
echo META_ROTATION_DONE
