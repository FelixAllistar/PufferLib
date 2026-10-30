#!/usr/bin/env bash
# Sweep status for tmux (poker_gen10_watch): current trial and its step count, finished trials with their scores.
cd /workspace/PufferLib
while true; do
  clear
  date
  d=$(ls -td checkpoints/poker/sweep_*/ 2>/dev/null | head -1)
  last=$(ls "$d" 2>/dev/null | grep -v magnet | sort | tail -1)
  echo "running: $(basename "$d")  steps $(( 10#${last%.bin} / 1000000 ))M"
  echo
  # only the current history (after the last restart that began a fresh one)
  awk '/starting a fresh history/{n=0; delete l} /^sweep run=|failed;/{l[n++]=$0} END{for(i=0;i<n;i++) print l[i]}' leagues/poker/gen10/sweep.log | tail -25
  sleep 30
done
