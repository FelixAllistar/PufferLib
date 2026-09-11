#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# != 2)); then
    echo "Usage: $0 CHECKPOINT OUTPUT.tar.gz" >&2
    echo "Set KAG_NATIVE_MACRO_MODE=2 or 3 (default: 2)." >&2
    exit 2
fi
kag_model=$1
kag_output=$2
kag_mode=${KAG_NATIVE_MACRO_MODE:-2}
kag_deterministic=${KAG_DETERMINISTIC:-1}
[[ $kag_deterministic == 0 || $kag_deterministic == 1 ]] || {
    echo "KAG_DETERMINISTIC must be 0 or 1" >&2; exit 2;
}
if [[ $kag_mode != 2 && $kag_mode != 3 ]]; then
    echo "KAG_NATIVE_MACRO_MODE must be 2 or 3" >&2
    exit 2
fi
[[ -f $kag_model ]] || { echo "Checkpoint not found: $kag_model" >&2; exit 1; }

kag_tmp=$(mktemp -d)
trap 'rm -rf "$kag_tmp"' EXIT
cp ocean/kaggriculture/submission/main.py "$kag_tmp/main.py"
cp ocean/kaggriculture/submission/native_macro_runtime.py "$kag_tmp/native_macro_runtime.py"
cp "ocean/kaggriculture/submission/native_macro_mode${kag_mode}.py" \
    "$kag_tmp/native_macro_mode.py"
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
kag_executor_version=$("$kag_python" ocean/kaggriculture/eval_observation_versions.py executor-version "$kag_model")
kag_extra_files=()
if [[ $kag_executor_version == 1 ]]; then
    [[ $kag_mode == 2 ]] || { echo "Executor 1 requires macro mode 2" >&2; exit 2; }
    cp ocean/kaggriculture/submission/native_macro_executor1.py "$kag_tmp/native_macro_executor1.py"
    kag_extra_files+=(native_macro_executor1.py)
elif [[ $kag_executor_version != 0 ]]; then
    echo "Unsupported executor version: $kag_executor_version" >&2; exit 2
fi
kag_obs_version=$("$kag_python" ocean/kaggriculture/eval_observation_versions.py version "$kag_model")
"$kag_python" -c 'import json,sys; json.dump({"observation_version":int(sys.argv[1]), "macro_executor_version":int(sys.argv[2]), "deterministic":bool(int(sys.argv[3]))}, open(sys.argv[4], "w"))' "$kag_obs_version" "$kag_executor_version" "$kag_deterministic" "$kag_tmp/policy_metadata.json"
"$kag_python" -m py_compile "$kag_tmp/main.py" \
    "$kag_tmp/native_macro_runtime.py" "$kag_tmp/native_macro_mode.py" \
    "$kag_tmp/native_macro_top_bot.py"
KAG_EXPORT_DIR="$kag_tmp" KAG_NATIVE_MACRO_MODE="$kag_mode" KAG_EXECUTOR_VERSION="$kag_executor_version" KAG_DETERMINISTIC="$kag_deterministic" \
    "$kag_python" -c \
    'import importlib.util, os; p=os.environ["KAG_EXPORT_DIR"]+"/main.py"; s=importlib.util.spec_from_file_location("kag_macro_export_preflight", p); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); assert m._NATIVE_MACRO is not None; assert m._NATIVE_MACRO.mode == int(os.environ["KAG_NATIVE_MACRO_MODE"]); assert m._NATIVE_MACRO.executor_version == int(os.environ["KAG_EXECUTOR_VERSION"]); assert m._DETERMINISTIC == bool(int(os.environ["KAG_DETERMINISTIC"])); assert m._MACRO_OVERLAY is None; print("native macro runtime preflight: OK")'
kag_sampling_args=()
[[ $kag_deterministic == 1 ]] || kag_sampling_args+=(--stochastic)
"$kag_python" ocean/kaggriculture/submission/test_model_export.py \
    "$kag_tmp/main.py" "$kag_tmp/kaggriculture_v4.bin" --mode smoke "${kag_sampling_args[@]}"
tar -C "$kag_tmp" -czf "$kag_output" main.py native_macro_runtime.py \
    native_macro_mode.py native_macro_top_bot.py kaggriculture_v4.bin policy_metadata.json "${kag_extra_files[@]}"
echo "Packaged native macro-mode-$kag_mode executor-$kag_executor_version deterministic=$kag_deterministic agent: $kag_output"
