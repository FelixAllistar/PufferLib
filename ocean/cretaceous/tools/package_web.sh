#!/usr/bin/env bash
# Packages the single-player web build: builds it (tools/build_web.sh), then
# packs the data next to it (tools/web_pack.py). The result is a directory of
# static files, served from relative paths, none larger than 14 MiB:
#
#   ocean/cretaceous/tools/package_web.sh [--assets DIR] [--out DIR] [--world FILE] [--no-build] [--keep-all-fields]
#
#   --assets DIR   assets directory, laid out like ocean/cretaceous/assets
#                  (textures/, foliage/, props/, dinos/, audio/ and their
#                  SOURCES.md / CREDITS.md); default ocean/cretaceous/assets.
#                  Whatever is missing is drawn with the game's fallbacks.
#   --out DIR      default build/cretaceous/web
#   --world FILE   the baked island; default the newest
#                  build/cretaceous/worlds/s1_n2048_*.terra. Run the native game
#                  once (./build/cretaceous/play) to bake it after a terra change.
#   --no-build     pack only (the wasm in --out is current)
#   --keep-all-fields  ship the world's bake-only fields (rain, soil, temperature)
#
# The cloud noise volumes (build/cretaceous/cloud_*.bin) ship prebaked when the
# native game has made them; everything else the renderer derives (terrain
# textures, ground material arrays, the plant atlas and impostors) is rebuilt in
# the browser at startup. Serve the directory with any static server, e.g.
#   python3 -m http.server -d build/cretaceous/web 8000
# Needs python3 with numpy (and Pillow for the 1K ground textures).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT"
ASSETS=ocean/cretaceous/assets; OUT=build/cretaceous/web; WORLD=""; BUILD=1; KEEP=""
while [ $# -gt 0 ]; do
    case "$1" in
        --assets) ASSETS=$2; shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        --world) WORLD=$2; shift 2 ;;
        --no-build) BUILD=0; shift ;;
        --keep-all-fields) KEEP=--keep-all-fields; shift ;;
        *) sed -n '2,25p' "$0" >&2; exit 2 ;;
    esac
done
[ -d "$ASSETS" ] || { echo "assets directory $ASSETS not found" >&2; exit 1; }
if [ $BUILD = 1 ]; then ocean/cretaceous/tools/build_web.sh "$OUT"; fi
[ -f "$OUT/cretaceous.wasm.bin" ] || { echo "no web build in $OUT (drop --no-build)" >&2; exit 1; }
CACHES=()
for f in cloud_shape_v2.bin cloud_detail_v3.bin; do
    [ -f "build/cretaceous/$f" ] && CACHES+=(--cache "build/cretaceous/$f=build/cretaceous/$f")
done
echo "Packing the data..."
python3 -I ocean/cretaceous/tools/web_pack.py --out "$OUT" --assets "$ASSETS" ${WORLD:+--world "$WORLD"} ${KEEP:+$KEEP} "${CACHES[@]}"
echo "Packaged: $OUT (serve it statically, e.g. python3 -m http.server -d $OUT 8000)"
