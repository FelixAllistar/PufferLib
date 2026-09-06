#!/bin/bash
# Run in an idle existing tmux pane; return the sweep to the user's main pane.
set -euo pipefail
cd "$(dirname "$0")/../.."
target_pid=${1:?current trainer PID required}
target_pane=${2:-ssh_tmux:0.0}
sweep_out=${3:-logs/kaggriculture/memory_sweep_4096_8192_v1}
[[ $target_pid =~ ^[0-9]+$ ]] || exit 2
[[ $sweep_out =~ ^[A-Za-z0-9_./-]+$ ]] || exit 2
echo "Queued memory sweep; waiting for trainer $target_pid. Will start in $target_pane."
while kill -0 "$target_pid" 2>/dev/null; do sleep 15; done
while pgrep -x puffer >/dev/null; do sleep 15; done
if [[ $(tmux display-message -p -t "$target_pane" '#{pane_current_command}') != bash ]]; then
    echo "Target pane is not an idle bash shell; leaving it alone. Start --run manually."
    exit 1
fi
tmux send-keys -l -t "$target_pane" "cd /workspace/PufferLib && set -o pipefail && /venv/main/bin/python ocean/kaggriculture/sweep_macro_memory.py --run --output $sweep_out 2>&1 | tee -a $sweep_out/queue.log"
tmux send-keys -t "$target_pane" Enter
echo "Sweep dispatched to $target_pane."
