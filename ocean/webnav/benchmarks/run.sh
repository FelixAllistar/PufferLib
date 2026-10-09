#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
if [ "${1:-}" = --confined ]; then
  shift
  node ocean/webnav/families/resource_guard.cjs >&2
  case "${1:-}" in
    build) exec make -f ocean/webnav/benchmarks/Makefile ;;
    test-rpc) exec node ocean/webnav/benchmarks/policy_rpc_test.cjs ;;
    test-response) exec node ocean/webnav/benchmarks/response_rpc_test.cjs ;;
    test-controls-rpc) exec node ocean/webnav/benchmarks/browser_controls_rpc_test.cjs ;;
    test-browser-response)
      export WEBNAV_PLAYWRIGHT_MODULE=${WEBNAV_PLAYWRIGHT_MODULE:-/mnt/d/puffertank/webnav-bench/node/node_modules/playwright-core}
      export WEBNAV_BROWSER_EXECUTABLE=${WEBNAV_BROWSER_EXECUTABLE:-$PWD/build/webnav/chrome-headless-shell-linux64/chrome-headless-shell}
      exec node ocean/webnav/benchmarks/browser_response_test.cjs ;;
    test-browser-contexts)
      export WEBNAV_PLAYWRIGHT_MODULE=${WEBNAV_PLAYWRIGHT_MODULE:-/mnt/d/puffertank/webnav-bench/node/node_modules/playwright-core}
      export WEBNAV_BROWSER_EXECUTABLE=${WEBNAV_BROWSER_EXECUTABLE:-$PWD/build/webnav/chrome-headless-shell-linux64/chrome-headless-shell}
      exec node ocean/webnav/benchmarks/browser_contexts_test.cjs ;;
    test-browser-shell)
      export WEBNAV_PLAYWRIGHT_MODULE=${WEBNAV_PLAYWRIGHT_MODULE:-/mnt/d/puffertank/webnav-bench/node/node_modules/playwright-core}
      export WEBNAV_BROWSER_EXECUTABLE=${WEBNAV_BROWSER_EXECUTABLE:-$PWD/build/webnav/chrome-headless-shell-linux64/chrome-headless-shell}
      exec node ocean/webnav/benchmarks/browser_shell_test.cjs ;;
    test-finish) exec /mnt/d/puffertank/webnav-bench/scorer-venv/bin/python ocean/webnav/benchmarks/finish_schema_test.py ;;
    test-app-policy) exec /mnt/d/puffertank/webnav-bench/scorer-venv/bin/python ocean/webnav/benchmarks/record_policy_test.py ;;
    test-contexts-policy) exec /mnt/d/puffertank/webnav-bench/scorer-venv/bin/python ocean/webnav/benchmarks/browser_contexts_policy_test.py ;;
    test)
      export WEBNAV_PLAYWRIGHT_MODULE=${WEBNAV_PLAYWRIGHT_MODULE:-/mnt/d/puffertank/webnav-bench/node/node_modules/playwright-core}
      export WEBNAV_BROWSER_EXECUTABLE=${WEBNAV_BROWSER_EXECUTABLE:-$PWD/build/webnav/chrome-headless-shell-linux64/chrome-headless-shell}
      exec node ocean/webnav/benchmarks/browser_view_test.cjs ;;
    eval) shift; exec node ocean/webnav/benchmarks/evaluate.cjs "$@" ;;
    calibrate) shift; exec node ocean/webnav/benchmarks/calibrate.cjs "$@" ;;
    score) shift; exec /mnt/d/puffertank/webnav-bench/scorer-venv/bin/webarena-verified eval-tasks "$@" ;;
    *) echo 'Use build, test, test-rpc, test-response, test-controls-rpc, test-browser-response, test-browser-contexts, test-browser-shell, test-finish, test-app-policy, test-contexts-policy, calibrate, eval or score' >&2; exit 2 ;;
  esac
fi
available_kib=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
bench_bytes=$(( (available_kib - 786432) * 1024 ))
# Reserve room for the separately capped 3 GiB website runtime.
if [ "$bench_bytes" -gt 2147483648 ]; then bench_bytes=2147483648; fi
if [ "$bench_bytes" -lt 536870912 ]; then echo 'Insufficient memory' >&2; exit 1; fi
mkdir -p build/webnav/families build/webnav/benchmarks
exec flock --nonblock --conflict-exit-code 75 build/webnav/families/resource.lock \
  systemd-run --user --scope -p "MemoryMax=$bench_bytes" -p MemorySwapMax=0 \
  -p TasksMax=128 -p CPUQuota=100% timeout --kill-after=5s "${WEBNAV_RUN_SECONDS:-1800}s" \
  bash ocean/webnav/benchmarks/run.sh --confined "$@"
