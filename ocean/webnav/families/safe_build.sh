#!/usr/bin/env bash
# Bound the whole compiler/build process tree, not only the Bun heap.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$repo_dir"
mkdir -p build/webnav/families
# Six GiB is the ceiling. Leave room for the host's existing processes when
# they use more memory than they did at the time the user set that ceiling.
available_kib=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
build_bytes=$(( (available_kib - 786432) * 1024 ))
if [ "$build_bytes" -gt 6442450944 ]; then build_bytes=6442450944; fi
if [ "$build_bytes" -lt 536870912 ]; then
  echo 'Insufficient available memory for a bounded WebNav build; retry when memory is free.' >&2
  exit 1
fi
if flock --nonblock --conflict-exit-code 75 build/webnav/families/resource.lock \
  systemd-run --user --scope \
  -p "MemoryMax=$build_bytes" -p MemorySwapMax=0 -p TasksMax=128 -p CPUQuota=100% \
  env WEBNAV_BUILD_CONFINED=1 timeout --signal=TERM --kill-after=5s 300s \
  node ocean/webnav/families/build.cjs "$@"; then
  exit 0
else
  build_status=$?
  if [ "$build_status" -eq 75 ]; then
    echo 'WebNav build busy: another family holds the resource lock (exit 75).' >&2
  else
    echo "WebNav capped build failed (exit $build_status); see diagnostics above." >&2
  fi
  exit "$build_status"
fi
