#!/bin/bash
# launch.sh RUN_ID [trainer overrides...]: one supervised training run in tmux ps_train.
if pgrep -x puffer_ps >/dev/null; then echo "a run is already going"; exit 1; fi
S=$(dirname "$0"); tmux kill-session -t ps_train 2>/dev/null
tmux new-session -d -s ps_train "$S/supervise.sh $*"
sleep 8; pgrep -x puffer_ps >/dev/null && echo "started $1" || echo "FAILED to start $1"
