#!/usr/bin/env bash
# Eval watcher for the Gen 3 1B run (2026-11-06), one eval job at a time beside training. For every checkpoint about
# 100M steps apart: win rate vs a random-move opponent (eval, bot_policy 1) and head to head vs the first checkpoint
# past 50M (REF, fixed once chosen), on random turn-1 positions. Appends to evals.txt. Uses puffer_match (same tree,
# built with seat 1 = policy 1 and policy_0_score, so match works; the running trainer binary is left alone).
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=4
D=reports/pokemon_experiments/20261106_gen3_1b
A="--base.env_name=pokemon3 --policy.hidden_size=1024 --policy.num_layers=2 --vec.total_agents=1024 --vec.num_threads=4"
last=0; REF=""
while :; do
  for ck in $(ls $D/checkpoints/*/*/*.bin 2>/dev/null | sort); do
    s=$((10#$(basename $ck .bin)))
    if [ -z "$REF" ] && [ $s -ge 50000000 ]; then REF=$ck; echo "REF $ck" >> $D/evals.txt; fi
    [ $s -lt $((last + 100000000)) ] && continue
    last=$s
    r=$(./puffer_match eval $ck $A --headless --env.bot_policy=1 2>&1 | grep -o "CUDA_EVAL.*" | tail -1)
    m=""; [ -n "$REF" ] && m=$(./puffer_match match $A --headless --base.eval_episodes=4000 --base.load_model_path=$ck --base.load_enemy_model_path=$REF 2>&1 | grep -o "CUDA_EVAL.*" | tail -1)
    echo "$(date -u +%H:%M) step=$s random: $r | vs_ref: $m" >> $D/evals.txt
  done
  if grep -q "^RUN_DONE" $D/evals.txt 2>/dev/null; then break; fi
  if ! tmux list-panes -t gen3:train >/dev/null 2>&1; then
    # training finished: one last pass on the final checkpoint, then stop
    [ -n "$final" ] && { echo RUN_DONE >> $D/evals.txt; break; }; final=1; last=$((last - 100000000)); continue
  fi
  sleep 120
done
