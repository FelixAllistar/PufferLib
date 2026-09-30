#!/usr/bin/env bash
set -euo pipefail
checkpoint=${1:?Usage: eval_tasks.sh CHECKPOINT [minimum_episodes]}
episodes=${2:-256}
semantic=${3:-0}
names=(click-test click-test-2 click-test-transfer click-dialog click-dialog-2 click-widget focus-text-2 click-checkboxes-transfer click-checkboxes-large click-checkboxes-soft)
mkdir -p build/webnav_family/eval
for i in "${!names[@]}"; do
    mask=$((1 << i))
    log="build/webnav_family/eval/${names[i]}.log"
    CUDA_HOME=${CUDA_HOME:-/usr/local/cuda} \
        systemd-run --user --scope --quiet -p MemoryMax=6G -p MemorySwapMax=0 \
        -p CPUQuota=100% timeout --signal=KILL 120 \
        build/webnav_family_train eval "$checkpoint" --headless \
        --base.eval_episodes="$episodes" --env.task_mask="$mask" \
        --env.seed_offset=1000000 --env.data_mode=1 \
        --env.semantic="$semantic" > "$log" 2>&1
    printf '%s ' "${names[i]}"
    rg '^CUDA_EVAL' "$log"
done
