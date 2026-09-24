#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
runner=build/webnav/evaluate
# Wrong clicks, successful solutions, invalid actions, backspace on empty/full
# inputs, text length limits, refocusing, Tab wrap, and Enter activation.
"$runner" browser 300
"$runner" browser 90 - 128 2 200000 expert
for actions in \
    1,6,11,11,7,5 \
    1,6,7,8,10,9,1,10,10,10,6,7,12 \
    6,7,10,2,3,4,1,7,6,12 \
    11,7,6,11,12 \
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0; do
    "$runner" browser 1 - 128 2 100003 "script:$actions"
done
"$runner" browser 1 - 128 2 100274 script:2,10,8,4,14,11,9,7,12
