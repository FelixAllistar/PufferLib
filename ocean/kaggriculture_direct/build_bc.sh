#!/usr/bin/env bash
# Build only. Never launches BC or modifies the live puffer binary.
set -euo pipefail
cd "$(dirname "$0")/../.."
cuda_root="${CUDA_HOME:-/usr/local/cuda}"
output="${1:-build/kaggriculture_direct_bc}"
mkdir -p "$(dirname "$output")"
exec "$cuda_root/bin/nvcc" -O2 --threads 2 -arch="${NVCC_ARCH:-native}" -std=c++17 \
    -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include -I"$cuda_root/include/cccl" \
    -DPUFFER_KAGGRICULTURE -DPUFFER_KAGGRICULTURE_DIRECT -DKAG_DIRECT_POLICY -DKAG_WITH_PAIRED_CRITIC \
    -DENV_NAME=kaggriculture '-DPUFFER_ENV_NAME="kaggriculture"' \
    '-DENV_HEADER="ocean/kaggriculture_direct/kaggriculture_direct.h"' \
    -Xcompiler=-fopenmp -Xcompiler=-Wno-narrowing \
    --diag-suppress=2361 --diag-suppress=111 --diag-suppress=128 \
    ocean/kaggriculture/bc.cu raylib-5.5_linux_amd64/lib/libraylib.a \
    -L"$cuda_root/lib64" -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand \
    -lm -lpthread -lomp5 -lGL -o "$output"
