#!/usr/bin/env bash
# Score a run's checkpoints every 500M (and the final one) on a team list with eval_teams.sh, until training
# is done. Usage: eval_ckpts_teams.sh RUN_DIR TEAMS_FILE
cd /workspace/PufferLib
R=$1; TEAMS=$2
while true; do
  done_flag=0; [ -f $R/trained ] && done_flag=1
  for ck in $(ls $R/checkpoints/pokemon/*/[0-9]*.bin 2>/dev/null | grep -v /0000000000000000.bin); do
    steps=$((10#$(basename $ck .bin))); last=$(ls $(dirname $ck)/[0-9]*.bin | tail -1)
    [ $((steps / 250000000 % 2)) = 0 ] || [ "$ck" = "$last" -a $done_flag = 1 ] || continue
    out=$R/team_eval/$(basename $ck .bin)
    grep -q EVALS_DONE $out/evals.log 2>/dev/null && continue
    bash reports/pokemon_experiments/20261023_next/eval_teams.sh $ck $out $TEAMS
    sed "s/^/$(basename $ck .bin) /" $out/evals.log | grep -v EVALS_DONE >> $R/evals.log
  done
  [ $done_flag = 1 ] && break
  sleep 300
done
echo EVALS_DONE >> $R/evals.log
