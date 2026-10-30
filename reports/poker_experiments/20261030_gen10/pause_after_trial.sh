#!/usr/bin/env bash
# Pause the gen10 sweep between trials (2026-10-30): wait for the running trial's result to land in the history,
# then stop the sweep before the next trial does any work. Resume later with the same command (history kept):
#   tmux new-session -d -s poker_gen10_sweep "./puffer sweep --base.env_name=poker 2>&1 | tee -a leagues/poker/gen10/sweep.log"
cd /workspace/PufferLib
H=leagues/poker/gen10/sweep.history
n=$(wc -l < $H)
until [ "$(wc -l < $H)" -gt "$n" ]; do sleep 15; done
tmux kill-session -t poker_gen10_sweep
for p in $(pgrep -x puffer); do kill $p; done
echo "$(date) paused after trial $(tail -n 1 $H | awk '{print $1}')" >> leagues/poker/gen10/pause.log
