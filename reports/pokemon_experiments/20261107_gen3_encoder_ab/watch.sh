#!/usr/bin/env bash
# Eval queue for the encoder A/B (2026-11-07), one eval job at a time beside training. Every checkpoint of both arms,
# policy (.bin) and magnet (.bin.magnet): vs the max-damage bot (env.bot_policy=2, 1,000 games) and vs poke-engine
# MCTS with 200 iterations (env.bot_policy=3, 500 games), on the 1,000 held-out teams, argmax. minibatch 1024: eval
# allocates training buffers, and the default 16384 does not fit beside a training run (12.3 of 16 GB).
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=6
D=reports/pokemon_experiments/20261107_gen3_encoder_ab
T=build/pokemon3/ou3
E="--base.env_name=pokemon3 --policy.hidden_size=256 --policy.num_layers=2 --vec.total_agents=1024 --vec.num_threads=6 --train.minibatch_size=1024 \
  --headless --env.sets=$T/sets.txt --env.pool=$T/heldout.txt --env.free_fraction=0 --env.matchup=1 --base.eval_greedy=1"
score() { "$@" 2>&1 | grep -o "perf=[0-9.]*" | tail -1; }
done_list=" "
while :; do
  for arm in struct flat; do
    n=$([ $arm = struct ] && echo 1 || echo 0)
    for ck in $(ls $D/checkpoints_$arm/*/*/*.bin 2>/dev/null | sort); do
      case "$done_list" in *" $ck "*) continue;; esac
      [ -f $ck.magnet ] || [ $((10#$(basename $ck .bin))) -eq 0 ] || continue  # wait for the magnet beside it
      done_list="$done_list$ck "
      s=$((10#$(basename $ck .bin)))
      line="arm=$arm step=$s"
      for w in "" .magnet; do
        [ -f $ck$w ] || continue
        md=$(score ./puffer_pk3s eval $E --policy.pokemon3_net=$n --env.bot_policy=2 --base.eval_episodes=1000 --base.load_model_path=$ck$w)
        mc=$(score ./puffer_pk3s eval $E --policy.pokemon3_net=$n --env.bot_policy=3 --base.eval_episodes=500 --base.load_model_path=$ck$w)
        line="$line | ${w:-.bin} maxdmg $md mcts200 $mc"
      done
      echo "$(date -u +%H:%M) $line" >> $D/evals.txt
    done
  done
  grep -q AB_DONE $D/status.txt 2>/dev/null && [ -z "$(ls $D/checkpoints_*/*/*/*.bin 2>/dev/null | while read c; do case "$done_list" in *" $c "*) ;; *) echo $c;; esac; done)" ] && { echo WATCH_DONE >> $D/evals.txt; break; }
  sleep 60
done
