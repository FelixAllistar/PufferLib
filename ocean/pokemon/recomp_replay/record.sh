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
  # muted: no window and no sound on the machine running it
  (cd "$GEN1RECOMP" && SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null PUFFER_REPLAY=1 REPLAY_JSON="$(realpath "$json")" SHOT_DIR="$frames" REPLAY_AUDIO_DIR="$frames/audio" \
    POKEPORT_DRIVER="$HERE/record_driver.lua" LD_LIBRARY_PATH="${LOVE_LIB:-}" "$LOVE" . > "$OUT/$name.log" 2>&1) || true
  # sound captured in game time (record_driver.lua REPLAY_AUDIO_DIR), mixed onto the video's timeline
  uv run -q --with numpy python "$HERE/mix_audio.py" "$frames/audio" "$frames/audio.wav" \
    "$(python3 -c "print($(ls "$frames" | grep -c '\.png$') / 30)")" >> "$OUT/$name.log" 2>&1 || true
  if [ -f "$frames/audio.wav" ]; then
    ffmpeg -loglevel error -y -framerate 30 -i "$frames/f%06d.png" -i "$frames/audio.wav" -c:v libx264 -pix_fmt yuv420p \
      -crf 20 -c:a aac -b:a 160k -shortest "$OUT/$name.mp4"
  else
    ffmpeg -loglevel error -y -framerate 30 -i "$frames/f%06d.png" -c:v libx264 -pix_fmt yuv420p -crf 20 "$OUT/$name.mp4"
  fi
  echo "$name: $(ls "$frames" | grep -c '\.png$') frames -> $OUT/$name.mp4"
  rm -rf "$frames"
done
