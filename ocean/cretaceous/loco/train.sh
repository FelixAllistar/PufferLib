#!/bin/bash
# Builds the trainer in locomotion mode and runs it with config/cretaceous_loco.ini
# (docs/locomotion/README.md, "Training"). Run from anywhere; needs the CUDA
# toolchain the trainer needs.
#   ocean/cretaceous/loco/train.sh [train|eval] [--section.key=value ...]
#   ocean/cretaceous/loco/train.sh --print [train|eval] [...]   only print the commands
# The trainer reads config/<its env name>.ini, and its env name is a build
# macro. Loco mode renames it to cretaceous_loco, so the trainer reads
# config/default.ini then config/cretaceous_loco.ini (never the game's
# config/cretaceous.ini) and logs runs under that name.
set -e
cd "$(dirname "$0")/../../.."
PRINT=0
if [ "$1" = "--print" ]; then PRINT=1; shift; fi
CMD=${1:-train}
[ $# -gt 0 ] && shift
BIN=build/cretaceous/puffer_loco
# build.sh splits NVCC_EXTRA on spaces without shell quoting, so the quotes
# reach the compiler and the macro is the string "cretaceous_loco"; build.sh
# passes NVCC_EXTRA after its own -DPUFFER_ENV_NAME, so the -U/-D pair wins.
FLAGS='-DCRET_LOCO -UPUFFER_ENV_NAME -DPUFFER_ENV_NAME="cretaceous_loco"'
if [ "$PRINT" = 1 ]; then
    echo "NVCC_EXTRA='${NVCC_EXTRA:+$NVCC_EXTRA }$FLAGS' ./build.sh cretaceous $BIN"
    echo "$BIN $CMD $*"
    exit 0
fi
mkdir -p build/cretaceous
NVCC_EXTRA="${NVCC_EXTRA:+$NVCC_EXTRA }$FLAGS" ./build.sh cretaceous "$BIN"
exec "$BIN" "$CMD" "$@"
