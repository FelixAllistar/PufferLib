#!/usr/bin/env bash
# Turn the gen10 sweep's live training dashboard off (sweep.live = 0) as soon as the running trial finishes (2026-11-01):
# the sweep window then prints one score line per trial. Not a swept range, so the history carries over.
cd /workspace/PufferLib
H=leagues/poker/gen10/sweep.history
n=$(wc -l < $H)
until [ "$(wc -l < $H)" -gt "$n" ]; do sleep 20; done
tmux kill-session -t poker_gen10_sweep
while pgrep -x puffer > /dev/null; do sleep 2; done
sed -i 's/^live = 1/live = 0/' config/poker.ini
tmux new-session -d -s poker_gen10_sweep "./puffer sweep --base.env_name=poker 2>&1 | tee -a leagues/poker/gen10/sweep.log"
echo "$(date) sweep.live off (score lines only)" >> leagues/poker/gen10/go_live.log
