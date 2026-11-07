#!/usr/bin/env bash
# Eval watcher for the Gen 3 OU run (2026-11-06), one eval job at a time beside training. Every 250M steps: head to
# head vs the starting model (randbat 1B) on 20K held-out OU positions (teams never trained on), 4K games, argmax.
# Every 500M: 30 gen3ou games vs Metamon Kakuna on its competitive teams (held out) through the Showdown bridge.
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=3
D=reports/pokemon_experiments/20261106_gen3_ou
START=reports/pokemon_experiments/20261106_gen3_1b/checkpoints/pokemon3/1793956256918/0000000999817216.bin
A="--base.env_name=pokemon3 --policy.hidden_size=1024 --policy.num_layers=2 --vec.total_agents=1024 --vec.num_threads=3 --headless"
last=-250000000; lastk=-500000000
while :; do
  for ck in $(ls $D/checkpoints/*/*/*.bin 2>/dev/null | sort); do
    s=$((10#$(basename $ck .bin)))
    [ $s -lt $((last + 250000000)) ] && continue
    last=$s
    m=$(./puffer_match match $A --env.positions=build/pokemon3/ou/heldout.txt --base.eval_episodes=4000 --base.eval_greedy=1 \
        --base.enemy_greedy=1 --base.load_model_path=$ck --base.load_enemy_model_path=$START 2>&1 | grep -o "score=[0-9.]*" | tail -1)
    k=""
    if [ $s -ge $((lastk + 500000000)) ]; then
      lastk=$s; bash $D/kakuna_eval.sh $ck 30 > /dev/null 2>&1; k=$(tail -1 $D/kakuna/summary.txt)
    fi
    echo "$(date -u +%H:%M) step=$s vs_start_heldout_ou: $m | kakuna: $k" >> $D/evals.txt
  done
  tmux list-panes -t gen3ou:train >/dev/null 2>&1 || { echo RUN_DONE >> $D/evals.txt; break; }
  sleep 120
done
