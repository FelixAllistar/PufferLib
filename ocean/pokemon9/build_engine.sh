#!/usr/bin/env bash
# Gen 9 engine for ocean/pokemon9: poke-engine (github.com/FelixAllistar/poke-engine, a fork of pmariglia/poke-engine)
# pinned at the gen9-merged branch, which merges every fix from the Showdown differential test (997/1000 positions
# agree; reports/pokemon_experiments/20261029_gen9_engine). Each fix is its own fix/* branch there. Builds the static
# library engine/ (C ABI for pokemon9.h) into build/pokemon9/.
set -euo pipefail
FORK=https://github.com/FelixAllistar/poke-engine
PIN=90f4d1a
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC=$ROOT/build/pokemon9/poke-engine
if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone -q "$FORK" "$SRC"
fi
if [ "$(git -C "$SRC" rev-parse --short=7 HEAD)" != "$PIN" ]; then
    git -C "$SRC" fetch -q "$FORK" gen9-merged
    git -C "$SRC" checkout -q -f "$PIN"
fi
cargo build --release --manifest-path "$HERE/engine/Cargo.toml" --target-dir "$ROOT/build/pokemon9/target"
cp "$ROOT/build/pokemon9/target/release/libpokemon9_engine.a" "$ROOT/build/pokemon9/"
echo "built build/pokemon9/libpokemon9_engine.a"
