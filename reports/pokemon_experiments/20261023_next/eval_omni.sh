#!/usr/bin/env bash
# Score an OMNI_MASTER run on several of its pool teams (48 games per panel team, argmax, seed 20261025),
# every 500M and the final checkpoint, until training is done. The evaluator reads our team from the
# checkpoint's config.ini, so each (checkpoint, team) gets a directory with a copy of the weights and that
# team's learner_team / learner_lead. Compare with the specialist on the same team: gar_egg_zap B0 62.5 / 57.6,
# zam_egg_cloy_don B0 58.3 / 58.0, zam_egg_zap B1 58.7 / 57.3. Usage: eval_omni.sh RUN_DIR
cd /workspace/PufferLib
R=$1
PY=build/pokemon/metamon-eval-env-cu128/bin/python
L=reports/pokemon_experiments/20261022_freepick_league/league/league.ini
TEAMS="gar_egg_zap zam_egg_cloy_don zam_egg_zap"
team_field() { awk -v name="$1" -v key="$2" '/^\[/{s=0} $1=="name"&&$3==name{s=1} s&&$1==key{print $3; exit}' $L; }
while true; do
  done_flag=0; [ -f $R/trained ] && done_flag=1
  for ck in $(ls $R/checkpoints/pokemon/*/[0-9]*.bin 2>/dev/null | grep -v /0000000000000000.bin); do
    steps=$((10#$(basename $ck .bin)))
    last=$(ls $(dirname $ck)/[0-9]*.bin | tail -1)
    [ $((steps / 250000000 % 2)) = 0 ] || [ "$ck" = "$last" -a $done_flag = 1 ] || continue
    for team in $TEAMS; do
      d=$R/omni_eval/$(basename $ck .bin)_$team; mkdir -p $d
      # A copy, not a symlink: evaluate_private.py resolves symlinks and would read the original config.ini.
      [ -f $d/$(basename $ck) ] || cp $ck $d/$(basename $ck)
      sed -e "s|^learner_team = .*|learner_team = $(team_field $team team)|" -e "s|^learner_lead = .*|learner_lead = $(team_field $team lead)|" $(dirname $ck)/config.ini > $d/config.ini
      for opp in SyntheticRLV2 TaurosV0; do
        out=$d/eval_$opp
        [ -f $out/summary.json ] && continue
        $PY ocean/pokemon/benchmarks/evaluate_private.py $d/$(basename $ck) --opponent $opp --mode argmax --device cuda --threads 1 \
            --games 48 --seed 20261025 --output $out > $out.log 2>&1
        echo "$(basename $ck) $team $opp $(python3 -c 'import json,sys;print(round(json.load(open(sys.argv[1]))["score"],3))' $out/summary.json 2>/dev/null || echo FAILED)" | tee -a $R/evals.log
      done
    done
  done
  [ $done_flag = 1 ] && break
  sleep 300
done
echo EVALS_DONE >> $R/evals.log
