#!/usr/bin/env bash
# Evaluates the run's newest checkpoint head to head (eval_h2h.sh, 8192) whenever it is >= 100M steps past the last
# one evaluated; stops after the run's final checkpoint. Results in h2h.txt.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261105_randbat_finetune; last=0
while :; do
  ck=$(ls $R/run/checkpoints/pokemon/*/0*.bin 2>/dev/null | sort | tail -1)
  if [ -n "$ck" ]; then
    s=$((10#$(basename $ck .bin)))
    if [ $((s - last)) -ge 100000000 ] || { grep -q "exit" $R/run/train.log && [ $s -ne $last ]; }; then
      bash $R/eval_h2h.sh $ck 8192; last=$s
    fi
  fi
  grep -q "exit" $R/run/train.log && [ "$s" = "$last" ] && break
  sleep 60
done
echo WATCH_DONE >> $R/h2h.txt
