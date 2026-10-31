#!/usr/bin/env bash
# Weedle specialist 2 (2026-10-31): run_weedle continued 1B steps against only the trainers it rarely beat (Lorelei,
# Bruno, Agatha, Lance, the three Champion teams), same rules as run_weedle.sh. Then the challenge with this checkpoint.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
C=ocean/pokemon/cartridge
ck=$(ls $R/run_weedle/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
mkdir -p $R/run_weedle2
./puffer_gen1cart train --base.load_model_path=$ck --base.checkpoint_dir=$R/run_weedle2/checkpoints \
  --base.log_dir=$R/run_weedle2/logs --env.trainer_file=$C/trainers.tsv --env.trainer_ratings=$C/trainer_ratings.txt \
  --env.trainer_pick=0 --env.trainer_perfect=0 --env.trainer_ai_player=$C/weedle.tsv \
  --env.trainer_ai_opponents=364,336,389,390,361,362,363 \
  --vec.num_policies=2 --vec.hist_policy_percent=1 --selfplay.enabled=1 --selfplay.initial_opponents=None \
  --train.total_timesteps=1000000000 > $R/run_weedle2/train.log 2>&1
echo "$(date) exit $?" >> $R/run_weedle2/train.log
RUN=run_weedle2 bash $R/ai_challenge.sh 20000 > $R/ai_challenge_run_weedle2.txt 2>&1
