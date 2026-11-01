#!/usr/bin/env bash
# Magikarp specialist (2026-11-01): the trainer fine-tune's checkpoint trained as a party of six level 100 Magikarp
# (Tackle, Splash; perfect stats, all badges) against the gym leaders, Giovanni, the Elite Four and the Champion at
# their original levels under the cartridge trainer AI (same setup as run_weedle.sh: side 2 frozen, AI-driven).
# Then magikarp_challenge.sh: parties of 1-6 Magikarp, how many it takes to beat each trainer.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
C=ocean/pokemon/cartridge
ck=$(ls $R/run/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
mkdir -p $R/run_magikarp
OMP_NUM_THREADS=8 ./puffer_gen1cart train --base.load_model_path=$ck --base.checkpoint_dir=$R/run_magikarp/checkpoints \
  --base.log_dir=$R/run_magikarp/logs --env.trainer_file=$C/trainers.tsv --env.trainer_ratings=$C/trainer_ratings.txt \
  --env.trainer_pick=0 --env.trainer_perfect=0 --env.trainer_ai_player=$C/magikarp6.tsv \
  --env.trainer_ai_opponents=337,338,339,340,341,343,342,276,364,336,389,390,361,362,363 \
  --vec.num_policies=2 --vec.hist_policy_percent=1 --selfplay.enabled=1 --selfplay.initial_opponents=None \
  --base.checkpoint_interval=${CKI:-400} --train.total_timesteps=${STEPS:-500000000} > $R/run_magikarp/train.log 2>&1
echo "$(date) exit $?" >> $R/run_magikarp/train.log
bash $R/magikarp_challenge.sh 5000 > $R/magikarp_challenge.txt 2>&1
