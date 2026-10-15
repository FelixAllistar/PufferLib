#!/usr/bin/env bash
# Config/launch contracts only: no actual trainer, CUDA job or Python process.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -O2 -std=c++17 -Wall -Wextra -Wno-unused-function \
    tests/test_goofspiel_sweep.cpp -o build/test_goofspiel_sweep
bash -n ocean/goofspiel/sweep.sh
./build/test_goofspiel_sweep
