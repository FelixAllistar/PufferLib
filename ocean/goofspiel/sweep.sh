#!/usr/bin/env bash
# Native config preparation / validation / launching. Never invokes Python.
set -euo pipefail
cd "$(dirname "$0")/../.."
goof_sweep_helper=build/goofspiel_sweep
mkdir -p build
if [[ ! -x $goof_sweep_helper || ocean/goofspiel/sweep.cpp -nt $goof_sweep_helper || ocean/goofspiel/training_config.h -nt $goof_sweep_helper || src/ini.h -nt $goof_sweep_helper ]]; then
    goof_sweep_tmp=$(mktemp build/goofspiel_sweep.XXXXXX)
    trap 'rm -f -- "$goof_sweep_tmp"' EXIT
    "${CXX:-g++}" -O2 -std=c++17 -Wall -Wextra -Wno-unused-function \
        ocean/goofspiel/sweep.cpp -o "$goof_sweep_tmp"
    mv -f -- "$goof_sweep_tmp" "$goof_sweep_helper"
    trap - EXIT
fi
exec "$goof_sweep_helper" "$@"
