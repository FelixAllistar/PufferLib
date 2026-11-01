#!/usr/bin/env bash
# Sweep status for tmux (poker_gen10_watch): current trial and its step count, finished trials with their scores.
cd /workspace/PufferLib
while true; do
  clear
  date
  d=$(ls -td checkpoints/poker/sweep_*/ 2>/dev/null | head -1)
  last=$(ls "$d" 2>/dev/null | grep -v magnet | grep '\.bin$' | sort | tail -1)
  # a trial that has not saved a checkpoint yet has no step count (an empty number used to kill this loop)
  steps=${last%.bin}; [ -n "$steps" ] && steps="$(( 10#$steps / 1000000 ))M" || steps="starting"
  echo "running: $(basename "$d")  steps $steps"
  echo
  # only the current history (after the last restart that began a fresh one)
  awk '/starting a fresh history/{n=0; delete l} /^sweep run=|failed;/{l[n++]=$0} END{for(i=0;i<n;i++) print l[i]}' leagues/poker/gen10/sweep.log | tail -25
  sleep 30
done
