#!/bin/sh
# Usage: sync_assets.sh SOURCE DEST
# Mirrors the file or directory SOURCE into DEST so that any number of
# independent builds may run it at the same time. Directories are created with
# mkdir -p, which accepts a directory another build created first. Each changed
# file is written under a private hidden name in its final directory and then
# renamed over the destination, so a concurrent copy or a running test only
# ever sees a complete file. Files that already match are left untouched.
set -eu
source=$1 dest=$2

place() {
    if [ -f "$2" ] && cmp -s "$1" "$2"; then return 0; fi
    staged="$(dirname "$2")/.$(basename "$2").sync.$$"
    if ! { cp "$1" "$staged" && mv -f "$staged" "$2"; }; then rm -f "$staged"; return 1; fi
}

if [ -d "$source" ]; then
    mkdir -p "$dest"
    (cd "$source" && find . -type d) | while IFS= read -r directory; do mkdir -p "$dest/$directory"; done
    (cd "$source" && find . ! -type d) | while IFS= read -r file; do place "$source/$file" "$dest/$file"; done
else
    mkdir -p "$(dirname "$dest")"
    place "$source" "$dest"
fi
