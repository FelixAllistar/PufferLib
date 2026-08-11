#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# != 2)); then
    echo "Usage: $0 CHECKPOINT OUTPUT.tar.gz" >&2
    exit 2
fi
kag_model=$1
kag_output=$2
[[ -f $kag_model ]] || { echo "Checkpoint not found: $kag_model" >&2; exit 1; }

kag_tmp=$(mktemp -d)
trap 'rm -rf "$kag_tmp"' EXIT
cp ocean/kaggriculture/submission/main.py "$kag_tmp/main.py"
cp "$kag_model" "$kag_tmp/kaggriculture_v4.bin"
../.venv/bin/python ocean/kaggriculture/submission/test_model_export.py \
    "$kag_tmp/main.py" "$kag_tmp/kaggriculture_v4.bin" --mode smoke
tar -C "$kag_tmp" -czf "$kag_output" main.py kaggriculture_v4.bin
echo "Packaged deterministic learned agent: $kag_output"
