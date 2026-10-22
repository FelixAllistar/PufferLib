#!/usr/bin/env bash
# Sample-mode TaurosV0 check (24/team = 72 games) of every master end-of-round
# checkpoint; the native hook only runs argmax. Writes <checkpoint>.metamon-sample/.
set -uo pipefail
cd /workspace/PufferLib
L=leagues/pokemon/rr20m_256x2_20261020
PY=build/pokemon/metamon-eval-env-cu128/bin/python
EVAL=build/pokemon-eval-v2/benchmarks/evaluate_private.py
LOG=reports/pokemon_experiments/20261020_roundrobin256_20m/sample_watch.log
while true; do
  for ck in $(ls $L/work/checkpoints/pokemon/r*-master-a*/0000000020971520.bin 2>/dev/null | sort); do
    out=$ck.metamon-sample
    [[ -f $out/summary.json ]] && continue
    [[ -f $ck.metamon/summary.json ]] || continue   # wait until the native argmax final is done
    rm -rf $out
    $PY $EVAL $ck --opponent TaurosV0 --mode sample --games 24 --seed 20261025 --device cuda --output $out > $out.log 2>&1 \
      || echo "FAILED $ck" >> $LOG
    round=$(echo $ck | grep -o 'r0000[0-9]*-master-a[0-9]*')
    echo "$(date -u +%FT%TZ) $round sample $(python3 -c "import json;s=json.load(open('$out/summary.json'));print(f\"{s['wins']}W/{s['draws']}D/{s['losses']}L {s['score']:.1%}\")" 2>/dev/null) argmax $(python3 -c "import json;s=json.load(open('$ck.metamon/summary.json'));print(f\"{s['wins']}W/{s['draws']}D/{s['losses']}L {s['score']:.1%}\")" 2>/dev/null)" >> $LOG
  done
  sleep 60
done
