#!/usr/bin/env bash
# OMNI eval matrix with the batched evaluator (2026-10-25): same games as run_eval_matrix.sh (each candidate pilots
# every team in pilot_teams.txt, 48 games per panel team, argmax, seed 20261025, vs SyntheticRLV2 and TaurosV0),
# but every candidate x team checkpoint shares one evaluate_batched.py run per opponent. Results already finished
# by run_eval_matrix.sh are kept (--skip-done); partial ones are restarted.
cd /workspace/PufferLib
N=reports/pokemon_experiments/20261023_next; M=$N/eval_matrix; TEAMS=$N/pilot_teams.txt
PY=build/pokemon/metamon-eval-env-cu128/bin/python
CANDIDATES="omni_master_1024:0000001999896576 omni_master_1024:0000001000341504 omni_master_256:0000001999896576 omni_master_256:0000001500512256"
cks=()
for c in $CANDIDATES; do
  run=${c%%:*}; step=${c##*:}; CK=$(ls $N/$run/checkpoints/pokemon/*/$step.bin)
  grep -v '^#' $TEAMS | while read name team lead; do
    [ -n "$name" ] || continue
    d=$M/${run}_$step/$name; mkdir -p $d
    [ -f $d/$step.bin ] || cp $CK $d/$step.bin  # a copy: evaluate_private resolves symlinks
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
  done
  for name in $(grep -v '^#' $TEAMS | awk 'NF{print $1}'); do cks+=($M/${run}_$step/$name/$step.bin); done
done
for opp in SyntheticRLV2 TaurosV0; do
  outs=()
  for ck in "${cks[@]}"; do
    out=$(dirname $ck)/eval_$opp
    [ -f $out/summary.json ] || rm -rf $out  # restart partial sequential evals
    outs+=($out)
  done
  $PY ocean/pokemon/benchmarks/evaluate_batched.py "${cks[@]}" --output "${outs[@]}" --skip-done --opponent $opp \
      --mode argmax --games 48 --seed 20261025 --batch 256 --workers 16 --threads 1 < /dev/null
done
# Table: candidate team opponent score
for d in $M/*/*/; do for opp in SyntheticRLV2 TaurosV0; do
  f=$d/eval_$opp/summary.json; [ -f $f ] && echo "$(basename $(dirname $d)) $(basename $d) $opp $(python3 -c 'import json,sys;print(round(json.load(open(sys.argv[1]))["score"],3))' $f)"
done; done > $M/matrix.txt
echo MATRIX_DONE
