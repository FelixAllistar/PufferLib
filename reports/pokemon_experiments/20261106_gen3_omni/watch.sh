#!/usr/bin/env bash
# Eval watcher for the fresh Gen 3 OU model (2026-11-06), one eval job at a time beside training. For every checkpoint
# at least 250M steps after the last one evaluated: head to head vs the previous evaluated checkpoint on the 1,000
# held-out teams (pool teams only, no drafts; 4K games, argmax both) and, every 500M, 30 gen3ou games vs Metamon Kakuna
# on its competitive teams through the Showdown bridge (both sides from that set: piloting, like earlier runs).
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=3
D=reports/pokemon_experiments/20261106_gen3_omni
T=build/pokemon3/ou3
A="--base.env_name=pokemon3 --policy.hidden_size=1024 --policy.num_layers=2 --vec.total_agents=1024 --vec.num_threads=3 --headless"
P="--env.sets=$T/sets.txt --env.pool=$T/heldout.txt --env.free_fraction=0"
last=-250000000; lastk=-500000000; prev=""
while :; do
  for ck in $(ls $D/checkpoints/*/*/*.bin 2>/dev/null | sort); do
    s=$((10#$(basename $ck .bin)))
    [ $s -lt $((last + 250000000)) ] && continue
    last=$s
    m=""
    [ -n "$prev" ] && m=$(./puffer_pk3 match $A $P --base.eval_episodes=4000 --base.eval_greedy=1 --base.enemy_greedy=1 \
        --base.load_model_path=$ck --base.load_enemy_model_path=$prev 2>&1 | grep -o "score=[0-9.]*" | tail -1)
    k=""
    if [ $s -ge $((lastk + 500000000)) ]; then
      lastk=$s; bash $D/kakuna_eval.sh $ck 30 > /dev/null 2>&1; k=$(tail -1 $D/kakuna/summary.txt)
    fi
    echo "$(date -u +%H:%M) step=$s vs_prev($(basename ${prev:-none} .bin))_heldout: $m | kakuna: $k" >> $D/evals.txt
    prev=$ck
  done
  tmux list-panes -t gen3omni:train >/dev/null 2>&1 || { echo RUN_DONE >> $D/evals.txt; break; }
  sleep 120
done
