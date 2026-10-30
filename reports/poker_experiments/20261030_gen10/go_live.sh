#!/usr/bin/env bash
# Restart the gen10 sweep with sweep.live = 1 as soon as the running trial finishes (2026-10-30), so the next trials
# show their dashboard in tmux poker_gen10_sweep. Ranges are unchanged, so the history carries over.
cd /workspace/PufferLib
H=leagues/poker/gen10/sweep.history
n=$(wc -l < $H)
until [ "$(wc -l < $H)" -gt "$n" ]; do sleep 20; done
tmux kill-session -t poker_gen10_sweep
while pgrep -x puffer > /dev/null; do sleep 2; done
tmux new-session -d -s poker_gen10_sweep "./puffer sweep --base.env_name=poker 2>&1 | tee -a leagues/poker/gen10/sweep.log"
echo "$(date) restarted live" >> leagues/poker/gen10/go_live.log
