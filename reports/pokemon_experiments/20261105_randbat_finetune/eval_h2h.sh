#!/usr/bin/env bash
# Head to head (2026-11-05): a randbat fine-tune checkpoint vs the ladder-run 2B (our best OU model) in our engine, on
# 2,000 held-out gen1randombattle teams (randbat_eval_teams.tsv, never trained on). Two tests, GAMES each:
#   mirror: both sides get the same random team (luck of the draw cancels out; the cleanest 1v1)
#   random: two different random teams (like the real ladder)
# Score = the fine-tune's win rate (draws half). Usage: eval_h2h.sh CHECKPOINT [GAMES] ; appends to h2h.txt
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261105_randbat_finetune
CK=$1; G=${2:-8192}; OLD=checkpoints/pokemon_final/20261028_ladder_2B.bin
for mode in mirror random; do
  m=0; [ $mode = mirror ] && m=1
  out=$(./puffer match --headless --base.env_name=pokemon --base.eval_episodes=$G \
    --base.load_model_path=$CK --base.load_enemy_model_path=$OLD --policy.hidden_size=1024 --policy.num_layers=2 \
    --env.trainer_file=$R/randbat_eval_teams.tsv --env.trainer_ratings=$R/randbat_eval_ratings.txt \
    --env.trainer_mirror=$m --env.trainer_window=1000000 --env.trainer_perfect=2 --env.trainer_pick=0 2>&1)
  line=$(echo "$out" | grep -o "CUDA_EVAL env=pokemon score=[^ ]* .*games=[0-9]*" | tail -1)
  echo "$(date -u +%H:%M) $(basename $(dirname $CK))/$(basename $CK) $mode $line" | tee -a $R/h2h.txt
  [ -z "$line" ] && echo "$out" | tail -20
done
