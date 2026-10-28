#!/usr/bin/env bash
# Disk guard for the gen3 sweep (32 GB box): each trial writes a snapshot every 40 epochs for its own opponent pool
# (~145 MB per 300M-step trial). Once a trial's directory has been idle for 30 minutes (trial finished), keep only its
# final checkpoint (+ .magnet, config.ini) and delete the intermediate snapshots. Runs every 10 minutes.
cd /workspace/PufferLib
while :; do
  for d in checkpoints/poker/sweep_*/; do
    [ -n "$(find "$d" -newermt '-30 minutes' -print -quit)" ] && continue
    last=$(ls "$d"*.bin 2>/dev/null | sort | tail -1); [ -z "$last" ] && continue
    for f in "$d"*.bin; do [ "$f" != "$last" ] && rm -f "$f" "$f.magnet"; done
  done
  sleep 600
done
