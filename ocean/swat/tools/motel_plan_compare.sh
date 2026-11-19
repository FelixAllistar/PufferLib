#!/bin/sh
# Plan hill-climb sheet: Luna Lodge HAER NM-13 existing site plan (public
# domain, rotated so its street is at the bottom, rescaled to 8 px/m like the
# plan render) beside a Briar Court plan render.
#   tools/motel_plan_compare.sh REF.jpg PLAN.png OUT.jpg LABEL
set -e
ref=$1; plan=$2; out=$3; label=$4
tmp=$(mktemp -d)
# Luna sheet 4 at 1024 px: 251'-2" (76.6 m) west wing spans ~415 px -> 5.42 px/m.
convert "$ref" -crop 720x270+215+300 +repage -rotate 90 -resize 147.6% -bordercolor white -border 8 \
  -gravity north -splice 0x22 -pointsize 14 -annotate +0+3 "Luna Lodge (HAER NM-13, PD), 8 px/m, street at bottom" "$tmp/ref.png"
convert "$plan" -gravity north -splice 0x22 -pointsize 14 -annotate +0+3 "$label" "$tmp/plan.png"
convert "$tmp/ref.png" "$tmp/plan.png" -background white -gravity center +append -quality 85 "$out"
rm -rf "$tmp"
