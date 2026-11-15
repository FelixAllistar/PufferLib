#!/usr/bin/env bash
# Scripted motel hitch measurement (see ROADMAP 2026-11-15 increment).
#
# Records the ordinary-input "combat exterior" motel completion run (courtyard
# approach, wall-charge breach, room entries, firing, squad orders, civilian and
# suspect reactions; 12,240 ticks), then replays it through performance_tool,
# which verifies every tick's digest and reports per-frame subsystem times,
# p50/p99/max and frames over 2x the median, plus the ten worst frames.
#
#   bash ocean/swat/benchmarks/hitch_run.sh OUT_DIR [MODE] [RUNS] [-- extra performance_tool args]
#
# MODE: cpu    --skip-raster: GL draws counted but not executed, so the
#              remaining main-thread time is game CPU + GL state/uploads
#              (the useful number under software GL; default)
#       raster full rendering at --size 320x180 (llvmpipe: GPU-side proxy)
#       native full rendering at the player's 1440x810 (use on a real GPU)
# Comparison toggles (same binary): SWAT_SHADOW_STAGGER=0 restores the shared
# 4-tick actor-shadow phase; pass "-- --audio-main-thread" for the old
# main-thread audio mixing pattern.
#
# Linux/cloud: runs under xvfb-run with a null ALSA device paced by the tool.
# Windows/GTX machine (from WSL, after bash ocean/swat/build-windows.sh):
# SWAT_HITCH_WINDOWS=1 uses build/swat/windows/*.exe, the real audio device and
# a hidden native window; e.g.
#   SWAT_HITCH_WINDOWS=1 bash ocean/swat/benchmarks/hitch_run.sh hitch-out native 3
#   SWAT_HITCH_WINDOWS=1 SWAT_SHADOW_STAGGER=0 bash ocean/swat/benchmarks/hitch_run.sh hitch-before native 3 -- --audio-main-thread
# Add "-- --gpu" there for GL timeline elapsed queries. Close other programs.
set -euo pipefail
OUT=${1:?usage: hitch_run.sh OUT_DIR [cpu|raster|native] [RUNS] [-- args]}; shift
MODE=${1:-cpu}; [ $# -gt 0 ] && shift
RUNS=${1:-3}; [ $# -gt 0 ] && shift
[ "${1:-}" = "--" ] && shift
EXTRA=("$@")
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
WINDOWS=${SWAT_HITCH_WINDOWS:-0}
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) WINDOWS=1;; esac
path() { if [ $WINDOWS = 1 ] && command -v wslpath >/dev/null; then wslpath -aw "$1"; else printf '%s' "$1"; fi; }
if [ $WINDOWS = 1 ]; then
    BIN=$ROOT/build/swat/windows; TOOL=$BIN/performance_tool.exe; COMPLETION=$BIN/test_motel_completion.exe
    export WSLENV=${WSLENV:+$WSLENV:}SWAT_SHADOW_STAGGER
    RUN=(); AUDIO=(--audio); HIDE=(--hidden)
else
    make -C "$ROOT/ocean/swat" performance-tool motel-completion-test-build -j4 >/dev/null
    BIN=$ROOT/build/swat; TOOL=$BIN/performance_tool; COMPLETION=$BIN/test_motel_completion
    printf 'pcm.!default { type null }\nctl.!default { type hw card 0 }\n' >"$OUT/asound.conf"
    export ALSA_CONFIG_PATH=$OUT/asound.conf
    RUN=(xvfb-run -a); AUDIO=(--audio-pace); HIDE=()
fi
REC=$OUT/motel-exterior.sgrp
[ -s "$REC" ] || (cd "$ROOT/ocean/swat" && "$COMPLETION" "$(path "$REC")" combat exterior >"$OUT/recording.log")
case "$MODE" in
    cpu) ARGS=(--seek 0 --frames 3570 --skip-raster);;
    raster) ARGS=(--seek 1400 --frames 600 --size 320x180 --gl-counts);;
    native) ARGS=(--seek 0 --frames 3570 --gl-counts "${HIDE[@]}");;
    *) echo "unknown mode $MODE" >&2; exit 1;;
esac
cd "$ROOT/ocean/swat"
for run in $(seq 1 "$RUNS"); do
    log=$OUT/$MODE-$run
    "${RUN[@]}" "$TOOL" --replay "$(path "$REC")" "${ARGS[@]}" "${AUDIO[@]}" --network --journal-profile \
        --frame-log "$(path "$log.csv")" "${EXTRA[@]}" >"$log.txt" 2>&1
    echo "== $MODE run $run ($log.txt)"
    grep -E '^(first_frame_ms|replay_verified|audio_thread|gl_draws|gl_vertices)' "$log.txt" || true
    grep -E '^(simulation|audio|network_snapshot_apply|journal_digest_or_append|pose|shadows|world_geometry|weapon|post|hud|draw_submit|present|total|total_cpu) mean' "$log.txt" || true
done
