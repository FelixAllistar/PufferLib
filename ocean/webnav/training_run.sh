#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mode=${1:-eval}
checkpoint=${2:-latest}
case "$mode" in
 eval|watch|browser) ;;
 *) echo 'Usage: bash ocean/webnav/training_run.sh eval|browser|watch [checkpoint|latest] [episodes] [task for browser/watch]' >&2; exit 2 ;;
esac
if [ "$checkpoint" = latest ]; then
 checkpoint=$(find build/webnav/training -type f -name '*.bin.webnav.json' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)
 checkpoint=${checkpoint%.webnav.json}
fi
if [ ! -f "$checkpoint" ]; then echo 'No new-profile checkpoint found. See ocean/webnav/TRAINING.md.' >&2; exit 1; fi
if [ "$mode" = eval ]; then
 exec build/webnav/training_eval "$checkpoint" "${3:-100}"
fi
if [ "$mode" = watch ]; then
 if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then echo 'Watch needs WSLg/X11. Use browser for headless evaluation.' >&2; exit 1; fi
 export WEBNAV_HEADED=1
 export WEBNAV_CHROME=${WEBNAV_CHROME:-build/webnav/chrome-linux64/chrome}
fi
exec build/webnav/training_browser "$checkpoint" "${4:-click-checkboxes}" "${3:-20}"
