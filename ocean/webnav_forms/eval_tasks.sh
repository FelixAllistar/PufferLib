#!/usr/bin/env bash
set -euo pipefail
checkpoint=${1:?Usage: eval_tasks.sh CHECKPOINT [minimum_episodes]}
episodes=${2:-256}
names=(enter-text-dynamic enter-text-2 enter-password text-transform copy-paste copy-paste-2 read-table-2 login-user-popup)
mkdir -p build/webnav_forms/eval
for i in "${!names[@]}"; do
    mask=$((1 << i))
    log="build/webnav_forms/eval/${names[i]}.log"
    CUDA_HOME=${CUDA_HOME:-/usr/local/cuda} \
        systemd-run --user --scope --quiet -p MemoryMax=6G -p MemorySwapMax=0 \
        -p CPUQuota=100% timeout --signal=KILL 120 \
        build/webnav_forms_train eval "$checkpoint" --headless \
        --base.eval_episodes="$episodes" --env.task_mask="$mask" \
        --env.seed_offset=1000000 --env.data_mode=0 --env.progress_reward=0 > "$log" 2>&1
    printf '%s ' "${names[i]}"
    rg '^CUDA_EVAL' "$log"
done
