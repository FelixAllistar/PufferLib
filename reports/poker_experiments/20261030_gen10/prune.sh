#!/usr/bin/env bash
# gen10 sweep disk guard (1024x2 checkpoints are 26 MB, plus a 26 MB .magnet each). Every 10 min: in every trial
# directory not written for 30 min (finished), keep only the newest .bin; in live ones keep the newest 3 .bin/.magnet
# pairs (the league pool has already loaded older snapshots into memory). If free space drops under 3 GB, delete the
# oldest finished trials' final checkpoints until it is back over 3 GB. Only trial directories created after the sweep
# started (newer than gen10/sweep_start) are touched; earlier runs' checkpoints are left alone.
MARK=/workspace/PufferLib/leagues/poker/gen10/sweep_start
cd /workspace/PufferLib/checkpoints/poker
while true; do
  for d in $(find . -mindepth 1 -maxdepth 1 -type d -newer $MARK -printf '%f\n'); do
    if [ -z "$(find "$d" -newermt '-30 minutes' -type f | head -1)" ]; then
      ls "$d"/*.bin 2>/dev/null | sort | head -n -1 | xargs -r rm -f
      rm -f "$d"/*.magnet
    else
      ls "$d"/*.bin 2>/dev/null | sort | head -n -3 | while read f; do rm -f "$f" "$f.magnet"; done
    fi
  done
  while [ "$(df --output=avail -B1G /workspace | tail -1)" -lt 3 ]; do
    old=$(find . -mindepth 1 -maxdepth 1 -type d -newer $MARK -printf '%T@ %f\n' | sort -n | cut -d' ' -f2 | while read d; do [ -z "$(find "$d" -newermt '-30 minutes' -type f | head -1)" ] && ls "$d"/*.bin >/dev/null 2>&1 && echo "$d" && break; done)
    [ -z "$old" ] && break
    echo "$(date) low disk: removing $old"; rm -rf "$old"
  done
  sleep 600
done
