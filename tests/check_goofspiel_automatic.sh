#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -O2 -std=c++17 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers \
    tests/test_goofspiel_automatic.cpp -o build/test_goofspiel_automatic
./build/test_goofspiel_automatic
