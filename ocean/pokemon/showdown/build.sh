#!/usr/bin/env bash
# Showdown bridge tooling. Parity: @pkmn/sim 0.9.31 (what pkmn/engine is validated against) bundled with pkmn's test
# patch; engine_replay links our Showdown-compatible engine build.
set -euo pipefail
cd "$(dirname "$0")/../../.."
P=build/pokemon/showdown_parity
[ -d $P/node_modules/@pkmn/sim ] || (mkdir -p $P && cd $P && npm init -y >/dev/null &&
  npm install --ignore-scripts --no-audit --no-fund @pkmn/sim@0.9.31 @pkmn/data@0.9.35 @pkmn/dex@0.9.35 esbuild@0.25.0)
NODE_PATH=$P/node_modules $P/node_modules/.bin/esbuild ocean/pokemon/showdown/battle.ts --bundle --platform=node \
  --format=cjs --log-level=warning --outfile=$P/battle.js
NODE_PATH=$P/node_modules $P/node_modules/.bin/esbuild ocean/pokemon/showdown/client.ts --bundle --platform=node \
  --format=cjs --log-level=warning --outfile=$P/client.js
clang -O2 -Iocean/pokemon ocean/pokemon/showdown/engine_replay.c build/pokemon/libpokemon.a -lm -o build/pokemon/engine_replay
make -C ocean/pokemon engine >/dev/null
clang -O2 -std=c11 -Wall -Wno-unused-function -Isrc -Iraylib-5.5_linux_amd64/include ocean/pokemon/showdown/policy_server.c \
  build/pokemon/libpokemon.a raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lrt -lm -o build/pokemon/policy_server
