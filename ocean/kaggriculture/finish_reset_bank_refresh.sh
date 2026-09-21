#!/usr/bin/env bash
# Post-build validation only; never starts training or edits config.
set -euo pipefail
cd /workspace/PufferLib
bank_dir=ocean/kaggriculture/state_bank/diverse_20260913
until [ -f "$bank_dir/summary.json" ]; do
    if ! tmux has-session -t reset-build-20260913 2>/dev/null; then
        echo 'Reset build stopped without a completion summary; inspect build.log.' >&2
        exit 1
    fi
    sleep 15
done
nice -n 10 /venv/main/bin/python ocean/kaggriculture/audit_diverse_reset_bank.py \
    --directory "$bank_dir" --config config/kaggriculture.ini
