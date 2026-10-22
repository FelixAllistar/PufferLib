#!/usr/bin/env bash
# SyntheticRLV2 vs TaurosV0 comparison. Two lanes (one per Metamon model), each
# sequential; finished cells (summary.json present) are skipped on rerun.
# Uses the deployed copy in build/pokemon-eval-v2 so the live league evaluator
# is untouched.
set -uo pipefail
cd /workspace/PufferLib
OUT=reports/pokemon_experiments/20261020_synthetic_rl_v2
PY=build/pokemon/metamon-eval-env-cu128/bin/python
EVAL=build/pokemon-eval-v2/benchmarks/evaluate_private.py
COMP=reports/pokemon_benchmarks/metamon_competitive_gen1ou
SEED=20261024
declare -A CK=(
  [r1]=leagues/pokemon/rr50m_256x2_20261019/members/master/r000001-a001/weights.bin
  [r6]=leagues/pokemon/rr50m_256x2_20261019/members/master/r000006-a001/weights.bin
  [r7]=leagues/pokemon/rr50m_256x2_20261019/members/master/r000007-a001/weights.bin
  [r26]=leagues/pokemon/rr50m_bf16_20261017/members/master/r000026-a001/weights.bin
)

cell() {  # model teamset checkpoint games_per_team
  local model=$1 set=$2 ck=$3 games=$4 dir=$OUT/$1_$2_$3
  [[ -f $dir/summary.json ]] && return
  rm -rf "$dir"
  local teams=(); [[ $set == competitive ]] && teams=(--team-dir $COMP)
  echo "$(date -u +%FT%TZ) start $model $set $ck"
  $PY $EVAL "${CK[$ck]}" --opponent $model --games $games --seed $SEED --device cuda \
      "${teams[@]}" --output "$dir" > "$dir.log" 2>&1 || echo "FAILED $model $set $ck (see $dir.log)"
  echo "$(date -u +%FT%TZ) done  $model $set $ck $(python3 -c "import json;s=json.load(open('$dir/summary.json'));print(f\"{s['wins']}W/{s['draws']}D/{s['losses']}L {s['score']:.2%}\")" 2>/dev/null)"
}

synthetic() {
  [[ -f $OUT/qualify_synthetic_r7_long.json ]] || $PY build/pokemon-eval-v2/benchmarks/qualify_metamon.py \
      "${CK[r7]}" --model SyntheticRLV2 --games 8 --output $OUT/qualify_synthetic_r7_long.json > $OUT/qualify_synthetic_r7_long.log 2>&1
  echo "$(date -u +%FT%TZ) qualification(long) done"
  cell SyntheticRLV2 panel r1 8
  for ck in r6 r7 r26; do cell SyntheticRLV2 panel $ck 100; done
  for ck in r6 r7 r26; do cell SyntheticRLV2 competitive $ck 8; done
}

tauros() {
  for ck in r6 r7; do cell TaurosV0 panel $ck 100; done
  for ck in r6 r7 r26; do cell TaurosV0 competitive $ck 8; done
}

synthetic > $OUT/lane_synthetic.log 2>&1 &
tauros > $OUT/lane_tauros.log 2>&1 &
wait
echo "$(date -u +%FT%TZ) matrix complete" | tee -a $OUT/lane_synthetic.log
