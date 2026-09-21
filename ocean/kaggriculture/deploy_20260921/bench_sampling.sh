#!/usr/bin/env bash
# Compile a separate diagnostic; never replace puffer or edit the live config.
set -euo pipefail
cd "$(dirname "$0")/../../.."
kag_bank=${1:?Usage: bench_sampling.sh RESET_BANK}
mkdir -p qualification
kag_out=$(mktemp -d qualification/sampler_20260921.XXXXXX)
exec > >(tee "$kag_out/results.log") 2>&1
printf 'Diagnostic output: %s\n' "$kag_out"
nvidia-smi --query-compute-apps=pid,process_name,used_memory --format=csv,noheader
kag_cuda=${CUDA_HOME:-${CUDA_PATH:-/usr/local/cuda}}
kag_nccl_flags=()
if [[ ! -f /usr/include/nccl.h && ! -f "$kag_cuda/include/nccl.h" ]]; then
    kag_nccl_root=$(uv run --no-project --python /venv/main/bin/python -c 'import nvidia.nccl; print(nvidia.nccl.__path__[0])')
    kag_nccl_flags+=("-I$kag_nccl_root/include" "-L$kag_nccl_root/lib" "-Xlinker=-rpath,$kag_nccl_root/lib")
fi
"$kag_cuda/bin/nvcc" -O2 --threads 2 -arch="${NVCC_ARCH:-sm_120}" -std=c++17 \
    -Xcompiler=-fopenmp -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include \
    -I"$kag_cuda/include/cccl" "${kag_nccl_flags[@]}" \
    tests/bench_kag_sampling.cu raylib-5.5_linux_amd64/lib/libraylib.a \
    -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand -lGL -lm -lpthread -lgomp \
    -o "$kag_out/bench_kag_sampling"
# Fail rather than compete with a training run restarted during compilation.
if [[ -n $(nvidia-smi --query-compute-apps=pid --format=csv,noheader) ]]; then
    printf 'GPU has an active compute process; diagnostic not launched.\n' >&2
    exit 1
fi
timeout 180s "$kag_out/bench_kag_sampling" "$kag_bank" 2048
timeout 180s "$kag_out/bench_kag_sampling" "$kag_bank" 4096
