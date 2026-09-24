#!/usr/bin/env bash
# Entire preparation -> labeling -> BC -> evaluation job lives on the GPU host.
# Start inside remote tmux, after transferring the immutable input archives/cache.
set -euo pipefail
cd /workspace/PufferLib
kag_base=/workspace/PufferLib/qualification/bc_expansion_20260923
kag_inputs="$kag_base/remote_inputs"
exec 9>"$kag_base/remote_pipeline.lock"
flock -n 9 || { echo 'Another remote BC pipeline owns this lock'; exit 1; }
if pgrep -x puffer >/dev/null || pgrep -x kag_bc >/dev/null; then
    echo 'A GPU trainer is already running; refusing concurrent jobs'; exit 1
fi
for kag_day in 17 18 19; do
    test -s "$kag_inputs/raw/kaggriculture-episodes-2026-09-$kag_day/kaggriculture-episodes-2026-09-$kag_day.zip"
done
test -s "$kag_inputs/cache_core.so"
kag_run=$(mktemp -d "$kag_base/remote_run.XXXXXX")
exec > >(tee "$kag_run/pipeline.log") 2>&1
trap 'kag_status=$?; printf "REMOTE PIPELINE EXIT=%s OUTPUT=%s\n" "$kag_status" "$kag_run"; printf "%s\n" "$kag_status" > "$kag_run/exit_status"' EXIT
echo "Entire pipeline is remote. Output: $kag_run"
echo 'No local process, uploaded-ready marker, or input-wait deadline is required.'
(cd "$kag_inputs" && sha256sum -c archives.sha256)
cp "$kag_base/profile.ini" "$kag_run/profile.ini"
cp config/kaggriculture.ini "$kag_run/active-config-before.ini"
sha256sum puffer config/kaggriculture.ini "$kag_inputs/cache_core.so" \
    ocean/kaggriculture/build/bc_expansion_20260923/kag_bc \
    ocean/kaggriculture/build/bc_expansion_20260923/libbc_replay.so \
    > "$kag_run/runtime-before.sha256"
echo 'PHASE 1: verify/reuse cached tapes and parse remaining games on this host.'
uv run --no-project --python /venv/main/bin/python python \
    ocean/kaggriculture/prepare_bc_replays.py \
    "$kag_inputs/raw/kaggriculture-episodes-2026-09-1[789]/*.zip" \
    --output "$kag_run/inventory" --teacher Majkel1337 --agent-name Majkel1337 \
    --cache-limit 10000 --cache-dir "$kag_inputs/tapes" \
    --lib "$kag_inputs/cache_core.so" --skip-incompatible \
    2>&1 | tee "$kag_run/preparation.log"
echo 'PHASE 2: opening audit, then current-controller labels and bounded BC candidates.'
uv run --no-project --python /venv/main/bin/python python \
    ocean/kaggriculture/audit_bc_openings.py "$kag_run/inventory/summary.json" \
    --output "$kag_run/openings.json"
uv run --no-project --python /venv/main/bin/python python \
    ocean/kaggriculture/run_expanded_bc.py \
    --manifest "$kag_run/inventory/summary.json" --profile "$kag_run/profile.ini" \
    --build ocean/kaggriculture/build/bc_expansion_20260923 \
    --output "$kag_run/comparison" --epochs 40 \
    --ppo-checkpoint checkpoints/kaggriculture/1790058597151/0000000299335680.bin
echo 'DONE: expanded BC candidates and fixed-opponent evaluations saved; no model promoted.'
