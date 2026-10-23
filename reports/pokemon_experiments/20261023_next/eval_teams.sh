#!/usr/bin/env bash
# Pilot generality: score one checkpoint piloting every team in a list (it drafts its own moves), 48 games per
# panel team vs SyntheticRLV2 and TaurosV0, argmax, seed 20261025. Each team gets a directory with a copy of
# the weights (evaluate_private.py resolves symlinks) and the checkpoint's config.ini with that team.
# Usage: eval_teams.sh CHECKPOINT OUT_DIR [TEAMS_FILE]
cd /workspace/PufferLib
CK=$1; OUT=$2; TEAMS=${3:-reports/pokemon_experiments/20261023_next/pilot_teams.txt}
PY=build/pokemon/metamon-eval-env-cu128/bin/python
mkdir -p $OUT
grep -v '^#' $TEAMS | while read name team lead; do
  [ -n "$name" ] || continue
  d=$OUT/$name; mkdir -p $d
  [ -f $d/$(basename $CK) ] || cp $CK $d/$(basename $CK)
  python3 - $(dirname $CK)/config.ini $d/config.ini "$team" "$lead" <<'PY'
import sys
src, dst, team, lead = sys.argv[1:]
out, section, done = [], None, set()
for line in open(src):
    s = line.strip()
    if s.startswith('['):
        if section == 'env':
            out += [f'{k} = {v}\n' for k, v in (('learner_team', team), ('learner_lead', lead)) if k not in done]
        section = s[1:-1]
    key = s.split('=')[0].strip() if '=' in s else None
    if section == 'env' and key in ('learner_team', 'learner_lead'):
        line = f'{key} = {team if key == "learner_team" else lead}\n'; done.add(key)
    out.append(line)
if section == 'env':
    out += [f'{k} = {v}\n' for k, v in (('learner_team', team), ('learner_lead', lead)) if k not in done]
open(dst, 'w').writelines(out)
PY
  for opp in SyntheticRLV2 TaurosV0; do
    out=$d/eval_$opp
    [ -f $out/summary.json ] && continue
    $PY ocean/pokemon/benchmarks/evaluate_private.py $d/$(basename $CK) --opponent $opp --mode argmax --device cuda --threads 1 \
        --games 48 --seed 20261025 --output $out > $out.log 2>&1 < /dev/null
    echo "$name $opp $(python3 -c 'import json,sys;print(round(json.load(open(sys.argv[1]))["score"],3))' $out/summary.json 2>/dev/null || echo FAILED)" | tee -a $OUT/evals.log
  done
done
echo EVALS_DONE >> $OUT/evals.log
