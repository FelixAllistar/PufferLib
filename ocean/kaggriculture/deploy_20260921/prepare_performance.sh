#!/usr/bin/env bash
# Run from the isolated candidate checkout; compile only, never use the GPU.
set -euo pipefail
cd "$(dirname "$0")/../../.."
kag_baseline=${1:?Usage: prepare_performance.sh BASELINE_SOURCE_ROOT}
kag_candidate=$PWD
[[ $(realpath "$kag_baseline") != "$kag_candidate" ]] || {
    echo 'Run this only in a separate candidate checkout, not the baseline install.' >&2; exit 1;
}
kag_out=$(mktemp -d qualification/performance.XXXXXX)
printf 'Performance qualification directory: %s\n' "$kag_out"
cp "$kag_baseline/config/kaggriculture.ini" "$kag_out/active-config.ini"
cp "$kag_out/active-config.ini" config/kaggriculture.ini
sha256sum "$kag_baseline/puffer" "$kag_baseline/config/kaggriculture.ini" > "$kag_out/baseline.sha256"
for kag_variant in baseline candidate; do
    kag_root=$kag_candidate
    [[ "$kag_variant" == baseline ]] && kag_root=$kag_baseline
    clang -O2 -std=c17 -I"$kag_root" -I"$kag_root/src" -I"$kag_root/vendor" \
        -I"$kag_root/raylib-5.5_linux_amd64/include" \
        "$kag_candidate/ocean/kaggriculture/tests/test_performance_replay.c" \
        "$kag_root/raylib-5.5_linux_amd64/lib/libraylib.a" \
        -lGL -lm -lpthread -o "$kag_out/cpu_$kag_variant"
    nice -n 10 timeout 240s "$kag_out/cpu_$kag_variant" \
        "$kag_baseline/ocean/kaggriculture/state_bank/diverse_20260913/full.kgb" \
        "$kag_out/cpu_$kag_variant.bin" | tee "$kag_out/cpu_$kag_variant.log"
done
cmp "$kag_out/cpu_baseline.bin" "$kag_out/cpu_candidate.bin"
echo 'PASS: baseline/candidate CPU masks, actions, decoding, RNG and game-state bytes.'
kag_pids=()
for kag_variant in baseline candidate; do
    kag_root=$kag_candidate
    [[ "$kag_variant" == baseline ]] && kag_root=$kag_baseline
    /usr/local/cuda/bin/nvcc -O2 --threads 2 -arch=sm_120 -std=c++17 -Xcompiler=-fopenmp \
        -I"$kag_root" -I"$kag_root/src" -I"$kag_root/vendor" \
        -I"$kag_root/raylib-5.5_linux_amd64/include" -I/usr/local/cuda/include/cccl \
        "$kag_candidate/tests/qualify_kag_performance.cu" \
        "$kag_root/raylib-5.5_linux_amd64/lib/libraylib.a" \
        -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand -lGL -lm -lpthread -lgomp \
        -o "$kag_out/gpu_$kag_variant" > "$kag_out/build_$kag_variant.log" 2>&1 &
    kag_pids+=("$!")
done
kag_failed=0
for kag_pid in "${kag_pids[@]}"; do wait "$kag_pid" || kag_failed=1; done
[[ "$kag_failed" == 0 ]] || { echo "Build failed; inspect $kag_out/build_*.log"; exit 1; }
echo "READY: $kag_out (no GPU job launched)"
