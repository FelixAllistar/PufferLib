#!/bin/bash
# Builds the trainer in locomotion mode and runs it with config/cretaceous_loco.ini
# (docs/locomotion/README.md, "Training"). Run from anywhere; needs the CUDA
# toolchain the trainer needs.
#   ocean/cretaceous/loco/train.sh [train|eval] [--section.key=value ...]
#   ocean/cretaceous/loco/train.sh --print [train|eval] [...]   only print the commands
# The trainer always reads config/cretaceous.ini for this env, so every key of
# config/cretaceous_loco.ini is passed as a --section.key=value override; the
# script's own arguments come last and win.
set -e
cd "$(dirname "$0")/../../.."
PRINT=0
if [ "$1" = "--print" ]; then PRINT=1; shift; fi
CMD=${1:-train}
[ $# -gt 0 ] && shift
BIN=build/cretaceous/puffer_loco
# [section] key = value  ->  --section.key=value (comments and blank lines skipped).
mapfile -t OVERRIDES < <(awk '
    /^[[:space:]]*\[/ { sec = $0; gsub(/[][[:space:]]/, "", sec); next }
    /^[[:space:]]*(#|;|$)/ { next }
    {
        line = $0; sub(/[[:space:]]*[#;].*$/, "", line)
        eq = index(line, "="); if (!eq) next
        key = substr(line, 1, eq - 1); val = substr(line, eq + 1)
        gsub(/[[:space:]]/, "", key); gsub(/^[[:space:]]+|[[:space:]]+$/, "", val)
        print "--" sec "." key "=" val
    }' config/cretaceous_loco.ini)
if [ "$PRINT" = 1 ]; then
    echo "NVCC_EXTRA=\"${NVCC_EXTRA:+$NVCC_EXTRA }-DCRET_LOCO\" ./build.sh cretaceous $BIN"
    echo "$BIN $CMD ${OVERRIDES[*]} $*"
    exit 0
fi
mkdir -p build/cretaceous
NVCC_EXTRA="${NVCC_EXTRA:+$NVCC_EXTRA }-DCRET_LOCO" ./build.sh cretaceous "$BIN"
exec "$BIN" "$CMD" "${OVERRIDES[@]}" "$@"
