#!/usr/bin/env bash
# Exploiter N (usage: ex_run.sh N MAIN_CKPT): starts from frozen main, 200M at tables of five copies of it (no bots, no
# pool, no PFSP), lr 0.001 (chess: main-initialized exploiters found holes fastest).
cd /workspace/PufferLib; K=reports/poker_kirby1; N=$1; M=$2
printf "poker_v1 6 1024 2 1\n$M\n" > $K/ex$N.league
OMP_NUM_THREADS=24 ./puffer_poker_k1 train --base.env_name=poker --base.load_model_path=$M \
  --base.checkpoint_dir=$K/ex$N/ck --base.log_dir=$K/ex$N/logs --base.run_id=ex$N --base.checkpoint_interval=0 \
  --env.native_league=$K/ex$N.league --env.league_pool_dir=None --env.rules_fraction=0 --env.league_pfsp=0 \
  --env.league_newest=0 --env.league_ramp=0 --train.total_timesteps=200000000 --train.learning_rate=0.001
