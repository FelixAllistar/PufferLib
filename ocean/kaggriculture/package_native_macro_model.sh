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
cp ocean/kaggriculture/submission/native_macro_runtime.py "$kag_tmp/native_macro_runtime.py"
cp ocean/kaggriculture/submission/top_bot/main.py "$kag_tmp/native_macro_top_bot.py"
cp "$kag_model" "$kag_tmp/kaggriculture_v4.bin"
if [[ -n ${KAG_PYTHON:-} ]]; then
    kag_python=$KAG_PYTHON
elif [[ -x ../.venv/bin/python ]]; then
    kag_python=../.venv/bin/python
elif [[ -x /venv/${ACTIVE_VENV:-main}/bin/python ]]; then
    kag_python=/venv/${ACTIVE_VENV:-main}/bin/python
else
    kag_python=$(command -v python3)
fi
"$kag_python" -m py_compile "$kag_tmp/main.py" \
    "$kag_tmp/native_macro_runtime.py" "$kag_tmp/native_macro_top_bot.py"
KAG_EXPORT_DIR="$kag_tmp" "$kag_python" -c \
    'import importlib.util, os; p=os.environ["KAG_EXPORT_DIR"]+"/main.py"; s=importlib.util.spec_from_file_location("kag_macro_export_preflight", p); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); assert m._NATIVE_MACRO is not None; assert m._MACRO_OVERLAY is None; print("native macro runtime preflight: OK")'
"$kag_python" ocean/kaggriculture/submission/test_model_export.py \
    "$kag_tmp/main.py" "$kag_tmp/kaggriculture_v4.bin" --mode smoke
tar -C "$kag_tmp" -czf "$kag_output" main.py native_macro_runtime.py \
    native_macro_top_bot.py kaggriculture_v4.bin
echo "Packaged deterministic native-macro agent: $kag_output"
