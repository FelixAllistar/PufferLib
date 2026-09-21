#!/usr/bin/env bash
# Qualified performance-only replacement of the real install. No config edits.
set -euo pipefail
[[ $# == 3 ]] || { echo 'Usage: activate_performance.sh CANDIDATE_ROOT SMOKE_DIRECTORY EXPECTED_OLD_BINARY_SHA256'; exit 1; }
kag_source=$(realpath "$1")
kag_smoke=$(realpath "$2")
kag_expected_old=$3
kag_target=/workspace/PufferLib
kag_binary=$kag_source/qualification/compact/puffer
[[ "$kag_source" != "$kag_target" && -f "$kag_smoke/PASS" ]]
grep -Fxq 'PASS: exact GPU rollout parity and all three PPO checkpoint byte comparisons.' \
    "$kag_source/qualification/compact/gpu_checks.log"
grep -Fxq -- "$(sha256sum "$kag_binary")" "$kag_smoke/smoke.log"
if [[ -n $(nvidia-smi --query-compute-apps=pid --format=csv,noheader) ]]; then
    echo 'GPU is busy; installation not attempted.' >&2; exit 1
fi
kag_old=$(sha256sum "$kag_target/puffer")
[[ "${kag_old%% *}" == "$kag_expected_old" ]] || {
    echo 'Installed binary changed since qualification; refusing to overwrite it.' >&2; exit 1;
}
kag_files=(src/pufferl.cu ocean/kaggriculture/multi_executor.h
    ocean/kaggriculture/action_feasibility.h ocean/kaggriculture/multi_market.h)
for kag_file in "${kag_files[@]}" puffer kaggriculture; do
    [[ -f "$kag_target/$kag_file" && ! -L "$kag_target/$kag_file" ]]
done
for kag_file in "${kag_files[@]}" kaggriculture; do [[ -f "$kag_source/$kag_file" ]]; done
kag_config_before=$(sha256sum "$kag_target/config/kaggriculture.ini")
(cd "$kag_target" && "$kag_binary" check kaggriculture)
mkdir -p /workspace/PufferLib-backups
kag_backup=$(mktemp -d /workspace/PufferLib-backups/pre-performance.XXXXXX)
tar -czf "$kag_backup/previous-runtime.tar.gz" -C "$kag_target" \
    puffer kaggriculture config/kaggriculture.ini "${kag_files[@]}"
tar -tzf "$kag_backup/previous-runtime.tar.gz"
printf 'Recoverable previous runtime: %s\n' "$kag_backup/previous-runtime.tar.gz"
for kag_file in "${kag_files[@]}" kaggriculture puffer; do
    kag_input=$kag_source/$kag_file
    [[ "$kag_file" == puffer ]] && kag_input=$kag_binary
    kag_staged=$(mktemp "$kag_target/$(dirname "$kag_file")/.perf-stage.XXXXXX")
    cp "$kag_input" "$kag_staged"
    chmod --reference="$kag_target/$kag_file" "$kag_staged"
    cmp "$kag_input" "$kag_staged"
    mv -T -- "$kag_staged" "$kag_target/$kag_file"
done
[[ $(sha256sum "$kag_target/config/kaggriculture.ini") == "$kag_config_before" ]]
(cd "$kag_target" && ./puffer check kaggriculture)
sha256sum "$kag_target/puffer" "$kag_target/kaggriculture" "$kag_target/config/kaggriculture.ini"
echo 'Installed qualified runtime. Config, datasets, BC trainer, checkpoints and running settings preserved. No training launched.'
