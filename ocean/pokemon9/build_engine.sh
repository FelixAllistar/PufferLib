#!/usr/bin/env bash
# Gen 9 engine for ocean/pokemon9 (2026-10-30): upstream pmariglia/poke-engine pinned at UPSTREAM, plus
# gen9_fixes.patch (the 22 mechanic fixes found by the Showdown differential test, 997/1000 positions agree; see
# reports/pokemon_experiments/20261029_gen9_engine), built into the static library engine/ (C ABI for pokemon9.h).
# Each fix also exists as its own atomic commit for upstream; this patch is their merge.
set -euo pipefail
UPSTREAM=f7b147f13637ceecc1b0d1e9172ec232af4d45d0
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC=$ROOT/build/pokemon9/poke-engine
if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone -q https://github.com/pmariglia/poke-engine "$SRC"
fi
if [ "$(git -C "$SRC" rev-parse HEAD)" != "$UPSTREAM" ] || ! git -C "$SRC" diff --quiet; then
    git -C "$SRC" checkout -q -f "$UPSTREAM"
    git -C "$SRC" clean -q -fd
fi
git -C "$SRC" apply "$HERE/gen9_fixes.patch"
cargo build --release --manifest-path "$HERE/engine/Cargo.toml" --target-dir "$ROOT/build/pokemon9/target"
git -C "$SRC" checkout -q -f "$UPSTREAM"
cp "$ROOT/build/pokemon9/target/release/libpokemon9_engine.a" "$ROOT/build/pokemon9/"
echo "built build/pokemon9/libpokemon9_engine.a"
