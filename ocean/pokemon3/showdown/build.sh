#!/usr/bin/env bash
# Builds the Gen 3 Showdown bridge (2026-11-06): policy_server (C, CPU) and client.js (Node bundle of client.ts).
# Needs build/pokemon3/libpokemon3_engine.a (ocean/pokemon3/build_engine.sh) and Node with esbuild + @pkmn/sim
# (build/pokemon/showdown_server/node_modules). Output in build/pokemon3/showdown/.
set -euo pipefail
cd "$(dirname "$0")/../../.."
OUT=build/pokemon3/showdown
mkdir -p $OUT
cc -O2 -o $OUT/policy_server ocean/pokemon3/showdown/policy_server.c build/pokemon3/libpokemon3_engine.a -lm -ldl -lpthread
echo "built $OUT/policy_server"
