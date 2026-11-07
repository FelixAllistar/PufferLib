#!/usr/bin/env bash
# Eval queue for the matchup A/B (2026-11-06), one eval job at a time beside training. First the fresh 1024x2 run's
# checkpoints (obs 5175, so puffer_pk3L: same code, matchup block compiled out) vs the max-damage bot, then every A/B
# checkpoint as it lands (puffer_pk3m with its arm's env.matchup). 1,000 games each on the 1,000 held-out teams, argmax.
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=3
D=reports/pokemon_experiments/20261106_gen3_matchup_ab
O=reports/pokemon_experiments/20261106_gen3_omni
T=build/pokemon3/ou3
E="--base.env_name=pokemon3 --policy.num_layers=2 --vec.total_agents=1024 --vec.num_threads=3 --headless \
  --env.sets=$T/sets.txt --env.pool=$T/heldout.txt --env.free_fraction=0 --env.bot_policy=2 --base.eval_episodes=1000 \
  --base.eval_greedy=1"
score() { "$@" 2>&1 | grep -o "perf=[0-9.]*\|draw=[0-9.]*" | tr '\n' ' '; }
while tmux list-panes -t gen3omni:train >/dev/null 2>&1 || ! grep -q RUN_DONE $O/evals.txt; do sleep 30; done
for s in 0000000262144000 0000001048576000 0000002097152000 0000003145728000; do
  ck=$(ls $O/checkpoints/*/*/$s.bin 2>/dev/null) || continue
  echo "$(date -u +%H:%M) omni1024 step=$((10#$s)) vs_maxdmg: $(score ./puffer_pk3L eval $E --policy.hidden_size=1024 --base.load_model_path=$ck)" >> $D/evals.txt
done
ck=$(ls $O/checkpoints/*/*/*.bin | sort | tail -1)
echo "$(date -u +%H:%M) omni1024 final $(basename $ck .bin) vs_maxdmg: $(score ./puffer_pk3L eval $E --policy.hidden_size=1024 --base.load_model_path=$ck)" >> $D/evals.txt
done_list=" "
while :; do
  for arm in on off; do
    m=$([ $arm = on ] && echo 1 || echo 0)
    for ck in $(ls $D/checkpoints_$arm/*/*/*.bin 2>/dev/null | sort); do
      case "$done_list" in *" $ck "*) continue;; esac
      done_list="$done_list$ck "
      echo "$(date -u +%H:%M) arm=$arm step=$((10#$(basename $ck .bin))) vs_maxdmg: $(score ./puffer_pk3m eval $E --policy.hidden_size=256 --env.matchup=$m --base.load_model_path=$ck)" >> $D/evals.txt
    done
  done
  grep -q AB_DONE $D/status.txt 2>/dev/null && [ -z "$(ls $D/checkpoints_*/*/*/*.bin 2>/dev/null | while read c; do case "$done_list" in *" $c "*) ;; *) echo $c;; esac; done)" ] && { echo WATCH_DONE >> $D/evals.txt; break; }
  sleep 60
done
