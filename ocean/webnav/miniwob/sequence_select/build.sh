#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../../../.." && pwd)"
cd "$repo_root"

bend_bin=${BEND_BIN:-/home/felix/.bend/bin/bend}
test "$(BEND_NO_TELEMETRY=1 "$bend_bin" --version)" = 'bend 2.0.6'

out_dir=build/webnav/sequence_select
mkdir -p "$out_dir"

BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/sequence_select/PROOF.bend
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/sequence_select/Wire.bend
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/sequence_select/Main.bend \
    -o "$out_dir/sequence_select_generated.c"

# The root webnav build owns the shared dispatcher and produces this object.
# Keeping the test linked to that object checks the exact ABI used by routing.
bridge_object=${SEQUENCE_SELECT_BRIDGE:-build/webnav/sequence-root/bridge.o}
test -f "$bridge_object"
clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Iocean/webnav \
    ocean/webnav/miniwob/sequence_select/test_native.c "$bridge_object" \
    -lpthread -lm -ldl -o "$out_dir/test_native"
"$out_dir/test_native"

sha256sum ocean/webnav/miniwob/sequence_select/*.bend \
    ocean/webnav/miniwob/sequence_select/*.h \
    ocean/webnav/miniwob/sequence_select/*.c \
    > "$out_dir/sources.sha256"
