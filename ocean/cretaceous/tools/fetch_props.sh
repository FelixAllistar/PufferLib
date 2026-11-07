#!/bin/bash
# Downloads the CC0 Poly Haven prop models (glTF, 1K textures) and converts
# them to game meshes + textures in ocean/cretaceous/assets/props with
# Blender (tools/props_convert.py). Run from the repository root.
#   ocean/cretaceous/tools/fetch_props.sh [ID ...]
# Downloads are untrusted data: each goes into its own directory and nothing
# in them is executed (Blender runs with --disable-autoexec).
set -e
BLENDER=${BLENDER:-$HOME/tools/blender-4.2.3-linux-x64/blender}
DL=${DL:-build/cret_props/dl}
OUT=ocean/cretaceous/assets/props
HERE=$(cd "$(dirname "$0")" && pwd)
IDS=("$@")
if [ ${#IDS[@]} -eq 0 ]; then
    IDS=(boulder_01 namaqualand_boulder_02 namaqualand_boulder_03 namaqualand_boulder_05 rock_07 rock_09
         rock_moss_set_01 rock_moss_set_02 stone_01 coast_rocks_01 namaqualand_rocks_01
         dead_tree_trunk dead_tree_trunk_02 tree_stump_01 tree_stump_02 root_cluster_01 bark_debris_01)
fi
mkdir -p "$DL" "$OUT"
for id in "${IDS[@]}"; do
    dir="$DL/$id"
    if [ ! -f "$dir/.done" ]; then
        rm -rf "$dir"; mkdir -p "$dir"
        curl -sf -m 60 -A cretaceous-fetch "https://api.polyhaven.com/files/$id" -o "$dir/files.json"
        python3 -I - "$dir/files.json" > "$dir/urls.txt" <<'PY'
import json, sys
g = json.load(open(sys.argv[1]))["gltf"]["1k"]["gltf"]
print(g["url"] + " " + g["url"].rsplit("/", 1)[1])
for rel, f in g["include"].items():
    if ".." in rel or rel.startswith("/"): raise SystemExit("bad path " + rel)
    print(f["url"] + " " + rel)
PY
        while read -r url rel; do
            mkdir -p "$dir/$(dirname "$rel")"
            curl -sf -m 120 -A cretaceous-fetch "$url" -o "$dir/$rel"
        done < "$dir/urls.txt"
        touch "$dir/.done"
        echo "downloaded $id"
    fi
    "$BLENDER" -b --factory-startup --disable-autoexec --python "$HERE/props_convert.py" -- "$dir/$id"_1k.gltf "$id" "$OUT" 2>&1 \
        | grep -E "^props:|Error|Traceback" || true
done
