#!/usr/bin/env bash
# Rebuild the conformed Mixamo bodies (git-ignored; Mixamo files are licensed,
# never committed). Source FBXs: /mnt/d/puffer_media/mixamo/characters.
#   tools/mixamo/build_assets.sh            # export (Blender) + conform all
set -euo pipefail
cd "$(dirname "$0")/../.."
RAW=${MIXAMO_RAW:-/mnt/d/puffer_media/mixamo}
BLENDER=${BLENDER:-$HOME/tools/blender-4.2.3-linux-x64/blender}
OUT=assets/characters/mixamo
# name role  (suspects: grey-hoodie Ch08, tactical-vest Ch18)
ROSTER="Ch08 suspect
Ch18 suspect
Ch06 civilian
Ch07 civilian
Ch12 civilian
Ch16 civilian
Ch17 civilian
Ch21 civilian
Ch22 civilian
Ch28 civilian
Ch31 civilian
Ch33 civilian
Ch37 civilian
Ch38 civilian
Remy civilian"
mkdir -p "$OUT"
BANKS="ready walk walk_left walk_right crouch_ready crouch_walk walk_backward"
: > "$OUT/index.txt.new"
while read -r name role; do
  src="$RAW/bodies/${name}_nonPBR/body.glb"; [ -f "$src" ] || src="$RAW/bodies/$name/body.glb"
  if [ ! -f "$src" ]; then
    fbx="${name}_nonPBR"; [ -f "$RAW/characters/$fbx.fbx" ] || fbx="$name"
    "$BLENDER" -b -P tools/mixamo/export_bodies.py -- "$RAW/characters" "$RAW/bodies" "$fbx" >/dev/null 2>&1
    src="$RAW/bodies/$fbx/body.glb"
  fi
  mkdir -p "$OUT/$name"
  # Civilian states: the BC_CIV01 clips retargeted onto the body's own skeleton.
  python3 tools/mixamo/retarget_clips.py "$src" assets/characters/civilian/runtime/BC_CIV01_candidate.glb "$OUT/$name/civ.glb"
  if [ "$role" = suspect ]; then
    for b in $BANKS; do
      tex=(); [ "$b" = ready ] && tex=(--textures "$OUT/$name")   # body_textures.txt once (same primitive order in every bank)
      python3 tools/mixamo/conform_body.py --rig officer "$src" "assets/characters/$b.glb" "$OUT/$name/$b.glb" "${tex[@]}"
    done
  fi
  echo "$name $role" >> "$OUT/index.txt.new"
done <<< "$ROSTER"
mv "$OUT/index.txt.new" "$OUT/index.txt"
echo "built $(wc -l < "$OUT/index.txt") bodies"
