#!/usr/bin/env bash
# Stock CPU Bend compiler with more frequent GC for memory-limited builders.
set -euo pipefail
node "$(dirname -- "${BASH_SOURCE[0]}")/resource_guard.cjs" >&2
bend_install="${BEND_HOME:-$HOME/.bend}"
bun_bin="${BUN_BIN:-$HOME/.bun/bin/bun}"
exec "$bun_bin" --smol "$bend_install/current/bend2/main.ts" "$@"
