#!/usr/bin/env bash
# Score each new checkpoint of an arm vs SyntheticRLV2 and TaurosV0 (48 games per panel team, argmax,
# seed 20261025) until training is done. One watcher per arm. Usage: eval_dir.sh RUN_DIR
cd /workspace/PufferLib
R=$1
PY=build/pokemon/metamon-eval-env-cu128/bin/python
while true; do
  done_flag=0; [ -f $R/trained ] && done_flag=1
  for ck in $(ls $R/checkpoints/pokemon/*/[0-9]*.bin 2>/dev/null | grep -v /0000000000000000.bin); do
    for opp in SyntheticRLV2 TaurosV0; do
      out=$ck.eval_$opp
      [ -f $out/summary.json ] && continue
      $PY ocean/pokemon/benchmarks/evaluate_private.py $ck --opponent $opp --mode argmax --device cuda --threads 1 \
          --games 48 --seed 20261025 --output $out > $out.log 2>&1
      echo "$(basename $ck) $opp $(python3 -c 'import json,sys;print(round(json.load(open(sys.argv[1]))["score"],3))' $out/summary.json 2>/dev/null || echo FAILED)" | tee -a $R/evals.log
    done
  done
  [ $done_flag = 1 ] && break
  sleep 300
done
echo EVALS_DONE >> $R/evals.log
