#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# < 1 || $# > 3)); then
    printf 'Usage: %s CHECKPOINT [TIMESTEPS=100000000] [RUNS=48]\n' "$0" >&2
    exit 2
fi

kag_checkpoint=$1
kag_timesteps=${2:-100000000}
kag_runs=${3:-48}

if [[ ! -f $kag_checkpoint ]]; then
    printf 'Checkpoint not found: %s\n' "$kag_checkpoint" >&2
    exit 1
fi

exec ./puffer sweep kaggriculture \
    "base.load_model_path=$kag_checkpoint" \
    "train.total_timesteps=$kag_timesteps" \
    "sweep.max_runs=$kag_runs"
