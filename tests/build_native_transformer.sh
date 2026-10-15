#!/usr/bin/env bash
# Build only the standalone benchmark; never replaces ./puffer.
set -euo pipefail
cd "$(dirname "$0")/.."
transformer_nvcc=${TRANSFORMER_NVCC:-/usr/local/cuda/bin/nvcc}
if [[ ! -x "$transformer_nvcc" ]]; then
    transformer_nvcc=$(command -v nvcc)
fi
transformer_arch=${TRANSFORMER_ARCH:-native}
transformer_out=${1:-build/bench_native_transformer}
mkdir -p "$(dirname "$transformer_out")"
"$transformer_nvcc" -O3 -lineinfo -std=c++17 -arch="$transformer_arch" \
    tests/bench_native_transformer.cu -lcublas -o "$transformer_out"
printf 'Built %s\n' "$transformer_out"
