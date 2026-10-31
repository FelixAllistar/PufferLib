#!/usr/bin/env bash
# Weedle specialist (2026-10-31): the trainer fine-tune's checkpoint trained 300M steps as a level 100 Weedle (Poison
# Sting, String Shot; perfect stats, all badges) against the hard in-game trainers at their original levels under the
# cartridge trainer AI. Side 2 is a frozen self-play bank whose actions the AI replaces, so only the Weedle side learns.
# Then the challenge (ai_challenge.sh) with this checkpoint.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
C=ocean/pokemon/cartridge
ck=$(ls $R/run/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
mkdir -p $R/run_weedle
./puffer_gen1cart train --base.load_model_path=$ck --base.checkpoint_dir=$R/run_weedle/checkpoints \
  --base.log_dir=$R/run_weedle/logs --env.trainer_file=$C/trainers.tsv --env.trainer_ratings=$C/trainer_ratings.txt \
  --env.trainer_pick=0 --env.trainer_perfect=0 --env.trainer_ai_player=$C/weedle.tsv \
  --env.trainer_ai_opponents=276,343,342,341,340,364,336,389,390,361,362,363 \
  --vec.num_policies=2 --vec.hist_policy_percent=1 --selfplay.enabled=1 --selfplay.initial_opponents=None \
  --train.total_timesteps=300000000 > $R/run_weedle/train.log 2>&1
echo "$(date) exit $?" >> $R/run_weedle/train.log
RUN=run_weedle bash $R/ai_challenge.sh 20000 > $R/ai_challenge_run_weedle.txt 2>&1
