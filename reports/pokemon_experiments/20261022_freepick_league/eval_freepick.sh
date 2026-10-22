#!/usr/bin/env bash
# Score every checkpoint of a free-pick run vs SyntheticRLV2 and TaurosV0, fully free-pick
# (48 games per panel team, argmax, seed 20261025). Usage: eval_freepick.sh [RUN_DIR]
cd /workspace/PufferLib
R=${1:-reports/pokemon_experiments/20261022_freepick_league/run}
PY=build/pokemon/metamon-eval-env-cu128/bin/python
for ck in $(ls $R/checkpoints/pokemon/*/[0-9]*.bin | grep -v /0000000000000000.bin); do
  for opp in SyntheticRLV2 TaurosV0; do
    out=$ck.eval_$opp
    [ -f $out/summary.json ] && continue
    $PY ocean/pokemon/benchmarks/evaluate_private.py $ck --opponent $opp --mode argmax --device cuda --threads 1 \
        --games 48 --seed 20261025 --output $out > $out.log 2>&1
    echo "$(basename $ck) $opp $(python3 -c 'import json,sys;print(round(json.load(open(sys.argv[1]))["score"],3))' $out/summary.json 2>/dev/null || echo FAILED)" | tee -a $R/evals.log
  done
done
