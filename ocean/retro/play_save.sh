#!/usr/bin/env bash
# Play a level and save hand-made practice starts (2026-11-01). Usage: play_save.sh LEVEL [BASE_ID]
# Keys: arrows; X or space = jump; Z or shift = run; F5/S = save this moment; F9/L = back to the last save; T = slow
# motion; P = hold to pause; R = restart. Close the window to write saved/retro/hand_LEVEL_DATE.archive.gz.
cd "$(dirname "$0")/../.."
L=${1:?level, e.g. 8-4}
mkdir -p saved/retro
OUT=saved/retro/hand_${L}_$(date +%Y%m%d_%H%M%S).archive.gz
./build/retro/viewer play "$L" --save-states "$OUT" --save-base "${2:-41}"
ls -la "$OUT" 2>/dev/null || echo "no moments saved"
