#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# < 2)); then
    echo "Usage: $0 OUTPUT.bc REPLAY.zip [REPLAY.zip ...]" >&2
    exit 2
fi

kag_output=$1
shift
kag_python=${KAG_TASK_PYTHON:-}
if [[ -z $kag_python ]]; then
    if [[ -x ../.venv/bin/python ]]; then
        kag_python=../.venv/bin/python
    elif [[ -x /venv/${ACTIVE_VENV:-main}/bin/python ]]; then
        kag_python=/venv/${ACTIVE_VENV:-main}/bin/python
    else
        kag_python=$(command -v python3)
    fi
fi

mkdir -p "$(dirname "$kag_output")"
exec "$kag_python" ocean/kaggriculture/import_elite_replays.py "$@" \
    --output "$kag_output" \
    --audit-json "${kag_output}.audit.json" \
    --manifest "${kag_output}.players.tsv" \
    --macro-mode tasks \
    --task-lookahead "${KAG_TASK_LOOKAHEAD:-32}" \
    --steps "${KAG_TASK_SEQUENCE_STEPS:-720}" \
    --exact-version "${KAG_TASK_VERSION:-1.32.7}" \
    --min-final-money "${KAG_TASK_MIN_FINAL_MONEY:-20000}"
