#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."
# Counts unfinished current round as one. Derive the remaining count on resume.
league_dir=leagues/pokemon/rr20m_256x2_20261020
rounds_remaining=$(node -e 'const s=require("./"+process.argv[1]+"/state.json");console.log(Math.max(0,50-s.round))' "$league_dir")
if (( rounds_remaining == 0 )); then
    echo 'All 50 planned rounds are complete.'
    exit 0
fi
exec node ocean/pokemon/freeleague.mjs train "$league_dir" --rounds "$rounds_remaining" --live
