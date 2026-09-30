#!/usr/bin/env bash
# Test existing family libraries together; never rebuild their Bend kernels.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$repo_dir"
if [ "${1:-}" != --confined ]; then
  available_kib=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
  suite_bytes=$(( (available_kib - 786432) * 1024 ))
  if [ "$suite_bytes" -gt 6442450944 ]; then suite_bytes=6442450944; fi
  if [ "$suite_bytes" -lt 536870912 ]; then
    echo 'Insufficient available memory for suite validation.' >&2
    exit 1
  fi
  mkdir -p build/webnav/families
  exec flock --nonblock --conflict-exit-code 75 build/webnav/families/resource.lock \
    systemd-run --user --scope -p "MemoryMax=$suite_bytes" -p MemorySwapMax=0 \
    -p TasksMax=128 -p CPUQuota=100% timeout --kill-after=5s 300s \
    bash ocean/webnav/families/test_suite.sh --confined
fi
node ocean/webnav/families/resource_guard.cjs
clang-19 -O2 -std=c11 -Wall -Wextra -Werror \
  ocean/webnav/families/common/test_suite.c \
  ocean/webnav/families/common/suite.c \
  ocean/webnav/families/common/loader.c \
  -ldl -lm -o build/webnav/families/test_suite
node <<'JS'
const cp = require('child_process');
const registry = require('./ocean/webnav/families/registry.json');
const libraries = Object.values(registry.families).map(f => f.library);
const result = cp.spawnSync('build/webnav/families/test_suite', libraries, {stdio:'inherit'});
if (result.error) throw result.error;
process.exit(result.status === null ? 1 : result.status);
JS
