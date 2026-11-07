#!/usr/bin/env bash
# Gen 3 engine for ocean/pokemon3: poke-engine (github.com/FelixAllistar/poke-engine, a fork of pmariglia/poke-engine)
# pinned on the gen3-diff branch, which has the 14 Gen 3 fixes from the Showdown differential test (984/991 positions
# agree; reports/pokemon_experiments/20261105_gen3_engine), one commit each. Builds the static library engine/ (C ABI
# for pokemon3.h) into build/pokemon3/.
set -euo pipefail
FORK=https://github.com/FelixAllistar/poke-engine
PIN=a1b1314
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC=$ROOT/build/pokemon3/poke-engine
if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone -q "$FORK" "$SRC"
fi
if [ "$(git -C "$SRC" rev-parse --short=7 HEAD)" != "$PIN" ]; then
    git -C "$SRC" fetch -q "$FORK" gen3-diff
    git -C "$SRC" checkout -q -f "$PIN"
fi
cargo build --release --manifest-path "$HERE/engine/Cargo.toml" --target-dir "$ROOT/build/pokemon3/target"
cp "$ROOT/build/pokemon3/target/release/libpokemon3_engine.a" "$ROOT/build/pokemon3/"
echo "built build/pokemon3/libpokemon3_engine.a"
