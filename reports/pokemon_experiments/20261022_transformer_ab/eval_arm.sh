#!/usr/bin/env bash
# Score every saved checkpoint of an arm vs SyntheticRLV2 and TaurosV0 (48 games per panel team,
# argmax, seed 20261025: the standard protocol). Appends "arm checkpoint opponent score" to evals.log.
set -u
ARM=$1
D=reports/pokemon_experiments/20261022_transformer_ab
PY=build/pokemon/metamon-eval-env-cu128/bin/python
for ck in $(ls $D/$ARM/checkpoints/pokemon/*/[0-9]*.bin | grep -v /0000000000000000.bin); do
  for opp in SyntheticRLV2 TaurosV0; do
    out=$ck.eval_$opp
    [ -f $out/summary.json ] || $PY ocean/pokemon/benchmarks/evaluate_private.py $ck --opponent $opp \
        --mode argmax --device cuda --threads 1 --games 48 --seed 20261025 --output $out > $out.log 2>&1
    score=$(python3 -c 'import json,sys;print(round(json.load(open(sys.argv[1]))["score"],3))' $out/summary.json 2>/dev/null || echo FAILED)
    echo "$ARM $(basename $ck) $opp $score" | tee -a $D/evals.log
  done
done
