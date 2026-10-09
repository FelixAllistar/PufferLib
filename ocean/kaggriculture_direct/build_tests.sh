#!/usr/bin/env bash
# Compile only; GPU execution is a separate, explicit operation.
set -euo pipefail
cd "$(dirname "$0")/../.."
cuda_root="${CUDA_HOME:-/usr/local/cuda}"
mkdir -p build
exec "$cuda_root/bin/nvcc" -O2 --threads 2 -arch="${NVCC_ARCH:-native}" -std=c++17 \
    -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include -I"$cuda_root/include/cccl" \
    -DPUFFER_KAGGRICULTURE -DPUFFER_KAGGRICULTURE_DIRECT -DKAG_DIRECT_POLICY -DKAG_WITH_PAIRED_CRITIC \
    -DENV_NAME=kaggriculture '-DPUFFER_ENV_NAME="kaggriculture"' \
    '-DENV_HEADER="ocean/kaggriculture_direct/kaggriculture_direct.h"' \
    -Xcompiler=-fopenmp -Xcompiler=-Wno-narrowing \
    --diag-suppress=2361 --diag-suppress=111 --diag-suppress=128 \
    ocean/kaggriculture_direct/tests/test_kernels.cu raylib-5.5_linux_amd64/lib/libraylib.a \
    -L"$cuda_root/lib64" -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand \
    -lm -lpthread -lomp5 -lGL -o build/test_kaggriculture_direct_kernels
