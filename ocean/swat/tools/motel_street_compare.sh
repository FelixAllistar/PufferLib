#!/bin/sh
# Street-level hill-climb sheet: a reference photo beside an engine capture,
# both scaled to the same height, labelled.
#   tools/motel_street_compare.sh REF.jpg CAPTURE.png OUT.jpg LABEL
set -e
tmp=$(mktemp -d)
convert "$1" -resize x420 -gravity north -background white -splice 0x20 -pointsize 14 -annotate +0+2 "reference" "$tmp/a.png"
convert "$2" -resize x420 -gravity north -background white -splice 0x20 -pointsize 14 -annotate +0+2 "$4" "$tmp/b.png"
convert "$tmp/a.png" "$tmp/b.png" +append -quality 85 "$3"
rm -rf "$tmp"
