#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_dir"
if [ -z "${CUDA_HOME:-}" ] && [ -x /usr/local/cuda/bin/nvcc ]; then
  export CUDA_HOME=/usr/local/cuda
fi
if [ "${1:-}" = --confined ]; then
  shift
  node ocean/webnav/families/resource_guard.cjs >&2
  case "${1:-test}" in
    test|build|evaluator|semantic-probe) exec make -f ocean/webnav_unified/Makefile "${1:-test}" ;;
    smoke) exec build/webnav_unified/puffer train --train.total_timesteps=32768 ;;
    train) shift; exec build/webnav_unified/puffer train "$@" ;;
    eval) shift; exec build/webnav_unified/native_eval "$@" ;;
    *) echo 'Use test, build, evaluator, semantic-probe, smoke, train or eval.' >&2; exit 2 ;;
  esac
fi
available_kib=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
unified_bytes=$(( (available_kib - 786432) * 1024 ))
if [ "$unified_bytes" -gt 6442450944 ]; then unified_bytes=6442450944; fi
if [ -n "${WEBNAV_MEMORY_MIB:-}" ]; then
  if ! [[ "$WEBNAV_MEMORY_MIB" =~ ^[0-9]{3,4}$ ]] ||
     [ "$WEBNAV_MEMORY_MIB" -lt 512 ] || [ "$WEBNAV_MEMORY_MIB" -gt 6144 ]; then
    echo 'WEBNAV_MEMORY_MIB must be in 512..6144.' >&2; exit 2
  fi
  requested_bytes=$((WEBNAV_MEMORY_MIB * 1048576))
  if [ "$unified_bytes" -gt "$requested_bytes" ]; then unified_bytes=$requested_bytes; fi
fi
if [ "$unified_bytes" -lt 536870912 ]; then
  echo 'Insufficient available memory for unified WebNav work.' >&2; exit 1
fi
mkdir -p build/webnav/families
case "${1:-test}" in
  train|eval) run_seconds=${WEBNAV_RUN_SECONDS:-3600} ;;
  *) run_seconds=300 ;;
esac
if ! [[ "$run_seconds" =~ ^[1-9][0-9]*$ ]]; then
  echo 'WEBNAV_RUN_SECONDS must be a positive integer.' >&2; exit 2
fi
exec flock --nonblock --conflict-exit-code 75 build/webnav/families/resource.lock \
  systemd-run --user --scope -p "MemoryMax=$unified_bytes" -p MemorySwapMax=0 \
  -p TasksMax=128 -p CPUQuota=100% timeout --kill-after=5s "${run_seconds}s" \
  bash ocean/webnav_unified/run.sh --confined "$@"
