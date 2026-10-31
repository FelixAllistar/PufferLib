#!/usr/bin/env bash
# Batch recording (2026-10-31): each replay JSON -> OUT_DIR/<name>.mp4 through Gen1Recomp offscreen (record_driver.lua).
# Usage: GEN1RECOMP=~/gen1recomp LOVE=.../bin/love [LOVE_LIB=.../lib] record.sh OUT_DIR replay.json...
# Extra REPLAY_* variables pass through to the driver (map, spot, warmup).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=$1; shift
mkdir -p "$OUT"
for json in "$@"; do
  name=$(basename "$json" .json)
  frames=$(mktemp -d)
  (cd "$GEN1RECOMP" && SDL_VIDEODRIVER=offscreen PUFFER_REPLAY=1 REPLAY_JSON="$(realpath "$json")" SHOT_DIR="$frames" \
    POKEPORT_DRIVER="$HERE/record_driver.lua" LD_LIBRARY_PATH="${LOVE_LIB:-}" "$LOVE" . > "$OUT/$name.log" 2>&1) || true
  ffmpeg -loglevel error -y -framerate 30 -i "$frames/f%06d.png" -c:v libx264 -pix_fmt yuv420p -crf 20 "$OUT/$name.mp4"
  echo "$name: $(ls "$frames" | wc -l) frames -> $OUT/$name.mp4"
  rm -rf "$frames"
done
