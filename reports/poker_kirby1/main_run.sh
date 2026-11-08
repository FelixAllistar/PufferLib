#!/usr/bin/env bash
# Poker Kirby loop v1, main (2026-11-08): gen13 recipe (config/poker.ini defaults) from gen13 trial 0's 600M final, 3B in
# one run (lr/entropy/magnet anneal over all of it), paused every 500M by loop.sh. Opponents: main.league banks (gen12 field
# + each cycle's exploiter) and the live pool of its own snapshots, PFSP-weighted (env.league_pfsp), half newest.
cd /workspace/PufferLib; K=reports/poker_kirby1
OMP_NUM_THREADS=24 ./puffer_poker_k1 train --base.env_name=poker --base.load_model_path=$K/banks/start_gen13t0.bin \
  --base.checkpoint_dir=$K/main/ck --base.log_dir=$K/main/logs --base.run_id=main --base.checkpoint_interval=100 \
  --env.native_league=$K/main.league --train.total_timesteps=3000000000 "$@"
