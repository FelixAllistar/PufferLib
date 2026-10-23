#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/poker
for players in 2 6 9; do
    clang-19 -O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra -DNDEBUG \
        -DPOKER_PLAYERS="$players" -shared -fPIC ocean/poker/native.c \
        -o "build/poker/libpoker$players.so"
    clang-19 -O3 -march=native -ffp-contract=off -std=c11 -DNDEBUG \
        -DPOKER_PLAYERS="$players" ocean/poker/test.c -o "build/poker/bench$players"
done
if [[ "${1:-}" == --test ]]; then
    # Reuse the checked stock CPU compiler and the same serialized cgroup guard.
    mkdir -p build/webnav/families
    flock --nonblock build/webnav/families/resource.lock \
        systemd-run --user --scope -p MemoryMax=2147483648 -p MemorySwapMax=0 \
        -p TasksMax=128 -p CPUQuota=100% \
        timeout --signal=TERM --kill-after=5s 300s \
        bash -c 'bash ocean/webnav/families/bend_cpu.sh ocean/poker/PROOF.bend &&
        bash ocean/webnav/families/bend_cpu.sh ocean/poker/Fixtures.bend -o build/poker/fixtures'
    for players in 2 6 9; do
        clang-19 -O2 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
            -DPOKER_PLAYERS="$players" ocean/poker/test.c -o "build/poker/test$players"
        "build/poker/test$players" 20000
        clang-19 -O2 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
            -DPOKER_PLAYERS="$players" ocean/poker/test_equity.c -lm \
            -o "build/poker/test_equity$players"
        "build/poker/test_equity$players"
    done
    clang-19 -O2 -g -std=c11 -fsanitize=address,undefined \
        -Isrc -Iraylib-5.5_linux_amd64/include ocean/poker/test_adapter.c \
        -lm -o build/poker/test_adapter
    build/poker/test_adapter
    clang-19 -O2 -g -march=native -std=c11 -fsanitize=address,undefined \
        -Isrc -Iraylib-5.5_linux_amd64/include ocean/poker/test_policy.c \
        -lm -o build/poker/test_policy
    build/poker/test_policy
    build/poker/test_adapter build/poker/test_league.txt
    # GPU-batched league (env.league_gpu) vs the inline CPU path; needs nvcc and a GPU.
    NVCC_BIN=$(command -v nvcc || echo "${CUDA_HOME:-/usr/local/cuda}/bin/nvcc")
    if [ -x "$NVCC_BIN" ] && nvidia-smi >/dev/null 2>&1; then
        "$NVCC_BIN" -O2 -std=c++17 -arch=native -Xcompiler=-fopenmp,-march=native \
            -Isrc -Iraylib-5.5_linux_amd64/include ocean/poker/test_league_gpu.cu -lcublas \
            -o build/poker/test_league_gpu
        build/poker/test_league_gpu build/poker/test_league.txt 1024 100
    fi
    "${POKER_PYTHON:-python3}" ocean/poker/test_eval.py
    "${POKER_PYTHON:-python3}" ocean/poker/verify.py
fi
