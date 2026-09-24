#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
bend_bin=${BEND_BIN:-$HOME/.bend/bin/bend}
test "$(BEND_NO_TELEMETRY=1 "$bend_bin" --version)" = 'bend 2.0.6'
mkdir -p build/webnav/tree
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/tree/PROOF.bend
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/tree/Main.bend -o build/webnav/tree/tree_generated.c
clang-19 -O3 -std=c11 -Ibuild/webnav/tree -c ocean/webnav/miniwob/tree/bridge.c \
    -o build/webnav/tree/tree_bridge.o
ar rcs build/webnav/tree/libtree.a build/webnav/tree/tree_bridge.o
clang-19 -O3 -std=c11 -I ocean/webnav/miniwob/tree \
    ocean/webnav/miniwob/tree/test_tree.c build/webnav/tree/libtree.a \
    -lpthread -lm -o build/webnav/tree/test_tree
build/webnav/tree/test_tree
sha256sum ocean/webnav/miniwob/tree/*.bend build/webnav/tree/tree_generated.c \
    > build/webnav/tree/sources.sha256
