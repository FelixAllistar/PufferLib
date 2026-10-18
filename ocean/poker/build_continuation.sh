#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 ]]; then
    echo 'Usage: build_continuation.sh [OUTPUT]' >&2
    exit 2
fi
if [[ "${1:-}" == --help ]]; then
    echo 'Usage: build_continuation.sh [OUTPUT]'
    echo 'Build only the optional HU context library; relative OUTPUT is repository-relative.'
    echo 'Default: build/poker/libpoker2_context.so. Set CC to choose the C compiler.'
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
output="${1:-build/poker/libpoker2_context.so}"
if [[ "$output" != /* ]]; then
    output="$repo_root/$output"
fi
mkdir -p "$(dirname "$output")"
compiler="${CC:-gcc}"

# This entry is additive: the existing build.sh and its 2/6/9-player defaults
# are untouched. Compile from this directory so source-relative includes and
# diagnostics do not depend on the caller's current directory.
cd "$script_dir"
"$compiler" -O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra -Werror \
    -DPOKER_PLAYERS=2 -DPOKER_RIVER_CFR=1 -shared -fPIC \
    continuation.c -lm -o "$output"
