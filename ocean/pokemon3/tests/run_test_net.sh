#!/bin/bash
# Build and run the structured Gen 3 net's CUDA/CPU parity + numerical-gradient test (FP32).
set -e
cd "$(dirname "$0")/../../.."
mkdir -p build/pokemon3
${CUDA_HOME:-/usr/local/cuda}/bin/nvcc -O2 --threads 2 -arch=${NVCC_ARCH:-native} -std=c++17 -I. -Isrc -Ivendor \
    -Iraylib-5.5_linux_amd64/include -Xcompiler=-fopenmp,-Wno-narrowing --diag-suppress 2361 \
    ocean/pokemon3/tests/test_net.cu raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lrt -lm \
    -lgomp -lcudart -lcublas -lcusolver -lcurand -lnccl -lnvidia-ml -o build/pokemon3/test_net
build/pokemon3/test_net
