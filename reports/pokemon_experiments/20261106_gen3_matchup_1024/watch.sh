#!/usr/bin/env bash
# Eval watcher for the matchup 1024x2 run (2026-11-06), one eval job at a time beside training. Every checkpoint at
# least 250M steps after the last one evaluated: vs the in-engine max-damage bot (1,000 held-out games, argmax; the
# early-warning test: the 256x2 A/B arm reached 53% at 500M, the old 1024x2 run 14% at 262M and 55% at 4B). Every 1B:
# 30 gen3ou games vs Metamon Kakuna through the Showdown bridge (kakuna_eval.sh).
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=3
D=reports/pokemon_experiments/20261106_gen3_matchup_1024
T=build/pokemon3/ou3
E="--base.env_name=pokemon3 --policy.hidden_size=1024 --policy.num_layers=2 --vec.total_agents=1024 --vec.num_threads=3 \
  --headless --env.sets=$T/sets.txt --env.pool=$T/heldout.txt --env.free_fraction=0 --env.matchup=1 --env.bot_policy=2 \
  --base.eval_episodes=1000 --base.eval_greedy=1"
last=-250000000; lastk=-1000000000
while :; do
  for ck in $(ls $D/checkpoints/*/*/*.bin 2>/dev/null | sort); do
    s=$((10#$(basename $ck .bin)))
    [ $s -lt $((last + 250000000)) ] && continue
    last=$s
    m=$(./puffer_pk3m eval $E --base.load_model_path=$ck 2>&1 | grep -o "perf=[0-9.]*" | tail -1)
    k=""
    if [ $s -ge $((lastk + 1000000000)) ]; then
      lastk=$s; bash $D/kakuna_eval.sh $ck 30 > /dev/null 2>&1; k=$(tail -1 $D/kakuna/summary.txt)
    fi
    echo "$(date -u +%H:%M) step=$s vs_maxdmg: $m | kakuna: $k" >> $D/evals.txt
  done
  tmux list-panes -t gen3m:train >/dev/null 2>&1 || { echo RUN_DONE >> $D/evals.txt; break; }
  sleep 120
done
