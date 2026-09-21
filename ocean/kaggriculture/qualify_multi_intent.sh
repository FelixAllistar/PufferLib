#!/usr/bin/env bash
# Bounded qualification only: no PPO sweep, replay downloads, or promotion.
set -euo pipefail

if [[ $# != 3 ]]; then
    echo "Usage: bash $0 DATASET BUILD_DIRECTORY NEW_OUTPUT_DIRECTORY" >&2
    exit 2
fi
kag_data=$(realpath -- "$1")
kag_build=$(realpath -- "$2")
kag_output=$(realpath -m -- "$3")
kag_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd -- "$kag_root"
kag_profile=ocean/kaggriculture/bc_2_2_baseline_20260921.ini

for kag_program in kag_bc test_cuda_adapter test_entity_policy test_kag_sampling; do
    [[ -x "$kag_build/$kag_program" ]] || {
        echo "Missing compiled qualification executable: $kag_build/$kag_program" >&2
        exit 2
    }
done
if pgrep -x puffer >/dev/null; then
    echo "Qualification deferred: an existing puffer process is running." >&2
    exit 2
fi
# Existing output directories are never reused, even after an interrupted run.
mkdir -- "$kag_output"
cp -- "$kag_profile" "$kag_output/profile.ini"
sha256sum -- "$kag_data" "$kag_build"/kag_bc "$kag_build"/test_* \
    > "$kag_output/input_sha256.txt"
nvidia-smi --query-gpu=name,compute_cap,memory.total --format=csv \
    > "$kag_output/gpu.csv"

kag_common=(bc.mode=train "bc.profile=$kag_profile" "bc.data=$kag_data"
    bc.seed=7 bc.batch=1 bc.learning_rate=0.00005 bc.report_interval=1)
CUDA_VISIBLE_DEVICES= "$kag_build/kag_bc" "${kag_common[@]}" \
    bc.verify_only=1 bc.value_coef=0.1 | tee "$kag_output/preflight.log"
for kag_program in test_cuda_adapter test_entity_policy test_kag_sampling; do
    "$kag_build/$kag_program" 2>&1 | tee "$kag_output/$kag_program.log"
done

TIMEFORMAT='qualification wall_seconds=%R'
for kag_variant in actor joint; do
    kag_value=0
    [[ "$kag_variant" == joint ]] && kag_value=0.1
    { time "$kag_build/kag_bc" "${kag_common[@]}" bc.verify_only=0 \
        bc.epochs=2 bc.load_model_path=None "bc.value_coef=$kag_value" \
        "bc.output=$kag_output/$kag_variant.bin"; } \
        2>&1 | tee "$kag_output/$kag_variant.log"
done
if cmp -s "$kag_output/actor.bin" "$kag_output/joint.bin"; then
    echo "Joint loss unexpectedly produced identical weights to actor-only BC." >&2
    exit 1
fi
"$kag_build/kag_bc" "${kag_common[@]}" bc.verify_only=0 bc.epochs=0 \
    bc.value_coef=0.1 "bc.load_model_path=$kag_output/joint.bin" \
    "bc.output=$kag_output/roundtrip.bin" 2>&1 | tee "$kag_output/roundtrip.log"
cmp -- "$kag_output/joint.bin" "$kag_output/roundtrip.bin"
printf '%s\n' 'PASS: qualification only; these two-epoch checkpoints are not playing-strength results.' \
    | tee "$kag_output/PASS.txt"
