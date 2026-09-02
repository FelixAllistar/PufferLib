#!/bin/bash
set -euo pipefail

# Build the submission-compatible hierarchical export.  The exact native MPC
# runner stays offline; a Kaggle callback has no serialized hidden opponent
# state or native shared library.  The archive therefore ships the proven
# deterministic top-bot executor as its default policy plus the public-state
# macro scorer/executor as an opt-in local layer.

cd "$(dirname "$0")/../.."

if (($# != 2)); then
    echo "Usage: $0 CHECKPOINT OUTPUT.tar.gz" >&2
    exit 2
fi

kag_model=$1
kag_output=$2
[[ -f $kag_model ]] || { echo "Checkpoint not found: $kag_model" >&2; exit 1; }

kag_macro_model=${KAG_MACRO_MODEL:-/home/felix/puffertank/macro_pilot_learned_final/macro_learned_72_ridge.npz}
[[ -f $kag_macro_model ]] || {
    echo "Macro model not found: $kag_macro_model" >&2
    echo "Set KAG_MACRO_MODEL to a portable .npz model." >&2
    exit 1
}

kag_tmp=$(mktemp -d)
trap 'rm -rf "$kag_tmp"' EXIT

cp ocean/kaggriculture/submission/main.py "$kag_tmp/main.py"
cp ocean/kaggriculture/submission/macro_overlay.py "$kag_tmp/macro_overlay.py"
cp ocean/kaggriculture/macro_actions.py "$kag_tmp/macro_actions.py"
cp ocean/kaggriculture/macro_executor.py "$kag_tmp/macro_executor.py"
cp ocean/kaggriculture/macro_value_model.py "$kag_tmp/macro_value_model.py"
cp ocean/kaggriculture/submission/top_bot/main.py "$kag_tmp/top_bot.py"
cp "$kag_model" "$kag_tmp/kaggriculture_v4.bin"
cp "$kag_macro_model" "$kag_tmp/macro_learned_72_ridge.npz"
cp ocean/kaggriculture/submission/MPC_README.txt "$kag_tmp/MPC_README.txt"

if [[ -n ${KAG_PYTHON:-} ]]; then
    kag_python=$KAG_PYTHON
elif [[ -x ../.venv/bin/python ]]; then
    kag_python=../.venv/bin/python
elif [[ -x /venv/${ACTIVE_VENV:-main}/bin/python ]]; then
    kag_python=/venv/${ACTIVE_VENV:-main}/bin/python
else
    kag_python=$(command -v python3)
fi

"$kag_python" -m py_compile \
    "$kag_tmp/main.py" "$kag_tmp/macro_overlay.py" \
    "$kag_tmp/macro_actions.py" "$kag_tmp/macro_executor.py" \
    "$kag_tmp/macro_value_model.py" "$kag_tmp/top_bot.py"

# The opening and recovery gates exercise imports, the model ABI, the exact
# top-bot tape, and the packaged file layout without spending minutes on a
# redundant full 720-frame JS harness.  Native parity and full offline MPC
# comparisons are run from the source tree separately.
PUFFERLIB_MACRO_OVERLAY=topbot \
"$kag_python" ocean/kaggriculture/submission/test_model_export.py \
    "$kag_tmp/main.py" "$kag_tmp/kaggriculture_v4.bin" --mode opening
PUFFERLIB_MACRO_OVERLAY=topbot \
"$kag_python" ocean/kaggriculture/submission/test_model_export.py \
    "$kag_tmp/main.py" "$kag_tmp/kaggriculture_v4.bin" --mode recovery

mkdir -p "$(dirname "$kag_output")"
tar -C "$kag_tmp" -czf "$kag_output" \
    main.py kaggriculture_v4.bin macro_overlay.py \
    macro_actions.py macro_executor.py macro_value_model.py top_bot.py \
    macro_learned_72_ridge.npz MPC_README.txt
echo "Packaged hierarchical deterministic Kaggriculture agent: $kag_output"
