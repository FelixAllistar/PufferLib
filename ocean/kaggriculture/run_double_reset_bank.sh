#!/usr/bin/env bash
# Expand the audited bank using additional games, then validate before use.
set -euo pipefail
cd /workspace/PufferLib
bank_previous=ocean/kaggriculture/state_bank/diverse_20260913
bank_expanded=ocean/kaggriculture/state_bank/diverse_20260913_double
nice -n 10 /venv/main/bin/python -u ocean/kaggriculture/build_diverse_reset_bank.py \
    /workspace/elite_replays/raw/kaggriculture-episodes-2026-08-{16..31}/kaggriculture-episodes-2026-08-*.zip \
    /workspace/elite_replays/raw/kaggriculture-episodes-2026-09-{01..12}/kaggriculture-episodes-2026-09-*.zip \
    --output "$bank_expanded" --reuse-bank "$bank_previous" \
    --episode-start 300 --episodes-per-day 1000 --jobs 6 \
    --min-full-states 157728 --reserve-gib 30
nice -n 10 /venv/main/bin/python -u ocean/kaggriculture/audit_diverse_reset_bank.py \
    --directory "$bank_expanded" --config config/kaggriculture.ini \
    --require-superset "$bank_previous" --min-full-states 157728
echo 'DOUBLE RESET BANK COMPLETE: minimum size, original snapshots, and native validation all passed.'
