#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/poker
compiler="${CC:-}"
if [[ -z "$compiler" ]]; then
    compiler="$(command -v clang-19 || command -v cc)"
fi
for players in 2 6 9; do
    "$compiler" -O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra -DNDEBUG \
        -DPOKER_PLAYERS="$players" -shared -fPIC ocean/poker/native.c \
        -o "build/poker/libpoker$players.so"
    "$compiler" -O3 -march=native -ffp-contract=off -std=c11 -DNDEBUG \
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
fi
if [[ "${1:-}" == --test || "${1:-}" == --native-test ]]; then
    # Portable simulator/policy checks; --native-test needs no Bend, systemd,
    # CUDA, raylib, or PufferNet checkout. Keep sanitizer leak settings supplied
    # by the caller so restricted sandboxes can disable only LeakSanitizer.
    for players in 2 6 9; do
        "$compiler" -O2 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
            -DPOKER_PLAYERS="$players" ocean/poker/test.c -o "build/poker/test$players"
        "build/poker/test$players" 20000
        "$compiler" -O2 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
            -DPOKER_PLAYERS="$players" ocean/poker/test_equity.c -lm \
            -o "build/poker/test_equity$players"
        "build/poker/test_equity$players"
        "$compiler" -O2 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined \
            -DPOKER_PLAYERS="$players" ocean/poker/test_river.c -lm \
            -o "build/poker/test_river$players"
        "build/poker/test_river$players"
    done
    for test in test_eval test_poker_audit test_slumbot_series test_dual_bench test_audit_archived_series; do
        "${POKER_PYTHON:-python3}" "ocean/poker/$test.py"
    done
fi
if [[ "${1:-}" == --test ]]; then
    "$compiler" -O2 -g -std=c11 -fsanitize=address,undefined \
        -Isrc -Iraylib-5.5_linux_amd64/include ocean/poker/test_adapter.c \
        -lm -o build/poker/test_adapter
    build/poker/test_adapter
    "$compiler" -O2 -g -march=native -std=c11 -fsanitize=address,undefined \
        -Isrc -Iraylib-5.5_linux_amd64/include ocean/poker/test_policy.c \
        -lm -o build/poker/test_policy
    build/poker/test_policy
    build/poker/test_adapter build/poker/test_league.txt
    "${POKER_PYTHON:-python3}" ocean/poker/verify.py
fi
