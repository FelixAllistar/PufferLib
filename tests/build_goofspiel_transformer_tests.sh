#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
transformer_nvcc=${TRANSFORMER_NVCC:-/usr/local/cuda/bin/nvcc}
transformer_arch=${TRANSFORMER_ARCH:-native}
transformer_output=${1:-build/test_goofspiel_transformer}
mkdir -p "$(dirname "$transformer_output")"
"$transformer_nvcc" -O2 -lineinfo -std=c++17 -arch="$transformer_arch" \
    -I. -Isrc -Ivendor -Iocean/goofspiel -Iraylib-5.5_linux_amd64/include \
    '-DENV_HEADER="ocean/goofspiel/goofspiel.h"' '-DPUFFER_ENV_NAME="goofspiel"' \
    -DPUFFER_GOOFSPIEL -DPRECISION_FLOAT -Xcompiler=-fopenmp \
    -Xcompiler=-Wno-narrowing --diag-suppress=2361 \
    tests/test_goofspiel_transformer.cu raylib-5.5_linux_amd64/lib/libraylib.a \
    -L/usr/local/cuda/lib64 -lcudart -lcublas -lcurand -lcusolver -lnccl -lnvidia-ml \
    -lGL -lm -lpthread -ldl -lrt -o "$transformer_output"
printf 'Built %s\n' "$transformer_output"
