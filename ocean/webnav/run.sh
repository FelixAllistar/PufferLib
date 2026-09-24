#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mode=${1:-watch}
case "$mode" in
    watch) backend=watch; episodes=${3:-20} ;;
    eval) backend=browser; episodes=${3:-1000} ;;
    native) backend=native; episodes=${3:-1000} ;;
    *) echo "Usage: bash ocean/webnav/run.sh watch|eval|native [checkpoint.bin|latest] [episodes]" >&2; exit 2 ;;
esac
checkpoint=${2:-latest}
if [ "$checkpoint" = latest ]; then
    checkpoint=$(find build/webnav/checkpoints/webnav -type f -name '*.bin' -printf '%T@ %p\n' | sort -nr | head -1 | cut -d' ' -f2-)
fi
if [ ! -f "$checkpoint" ]; then
    echo "No checkpoint found. Train with: make -f ocean/webnav/Makefile train" >&2
    exit 1
fi
if [ "$backend" = watch ]; then
    if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        echo "Watch needs a graphical display (WSLg or X11). Use eval for headless browser evaluation." >&2
        exit 1
    fi
    if [ -z "${WEBNAV_CHROME:-}" ] && [ ! -x build/webnav/chrome-linux64/chrome ]; then
        bash ocean/webnav/setup_browser.sh headed
    fi
elif [ "$backend" = browser ] && [ -z "${WEBNAV_CHROME:-}" ] && [ ! -x build/webnav/chrome-headless-shell-linux64/chrome-headless-shell ]; then
    bash ocean/webnav/setup_browser.sh
fi
# Recompile the small evaluator so source edits take effect without rebuilding Bend.
if [ ! -f build/webnav/libwebnav.a ]; then bash ocean/webnav/build.sh; fi
clang-19 -O3 -std=c11 -Isrc -Ivendor -Iocean/webnav ocean/webnav/evaluate.c \
    ocean/webnav/cdp.c ocean/webnav/policy.c vendor/cJSON.c build/webnav/libwebnav.a \
    -lm -lpthread -o build/webnav/evaluate
echo "Checkpoint: $checkpoint" >&2
echo "Mode: $mode | Episodes: $episodes | Ctrl+C stops evaluation" >&2
exec build/webnav/evaluate "$backend" "$episodes" "$checkpoint"
