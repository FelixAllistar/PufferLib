#!/usr/bin/env bash
# Evaluates every checkpoint of the run with the fixed (v2) evaluator, argmax and
# sample mode, vs TaurosV0 on the panel. Interim 24/team; final 100/team.
set -uo pipefail
cd /workspace/PufferLib
EXP=reports/pokemon_experiments/20261020_r26_anneal
CK=$EXP/work/checkpoints/pokemon/r26_anneal_503m
PY=build/pokemon/metamon-eval-env-cu128/bin/python
EVAL=build/pokemon-eval-v2/benchmarks/evaluate_private.py
FINAL=0000000503316480.bin
mkdir -p $EXP/evals
while true; do
  for ck in $(ls $CK/*.bin 2>/dev/null | sort); do
    name=$(basename $ck .bin); [[ -f $ck.ini && -f $CK/config.ini ]] || continue
    games=24; [[ $(basename $ck) == $FINAL ]] && games=100
    for mode in argmax sample; do
      out=$EXP/evals/${name}_$mode
      [[ -f $out/summary.json ]] && continue
      rm -rf $out
      $PY $EVAL $ck --opponent TaurosV0 --mode $mode --games $games --seed 20261024 --device cuda --output $out > $out.log 2>&1 \
        || echo "FAILED $name $mode"
      echo "$(date -u +%FT%TZ) $name $mode $(python3 -c "import json;s=json.load(open('$out/summary.json'));print(f\"{s['wins']}W/{s['draws']}D/{s['losses']}L {s['score']:.1%} n={s['games']}\")" 2>/dev/null)"
    done
  done
  [[ -f $EXP/evals/0000000503316480_sample/summary.json ]] && { echo "$(date -u +%FT%TZ) all evaluations complete"; break; }
  sleep 60
done
