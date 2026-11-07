#!/bin/sh
# Rebuild the git-ignored suspect rifles (assets/weapons/kitbash) from the
# licensed Gun Kitbash Vol 3 FBXs. Never commit the outputs (public repo).
#   tools/kitbash/build_assets.sh [FBX_DIR] [BLENDER]
# Grip shifts (metres) were measured at hand height on `fit_guns.py measure`
# side views, putting each pistol grip on the carbine's grip landmark.
set -e
here=$(cd "$(dirname "$0")" && pwd); swat=$(cd "$here/../.." && pwd)
fbx=${1:-/mnt/d/puffer_media/kitbash/fbx}
blender=${2:-$HOME/tools/blender-4.2.3-linux-x64/blender}
out="$swat/assets/weapons/kitbash"; mkdir -p "$out"
"$blender" -b -P "$here/fit_guns.py" -- export "$fbx" "$swat/assets/weapons/rifle7_rigid_textured.glb" "$out" \
    30:-0.014 11:-0.006 32:0.039 20:0.048 16:-0.056 2>&1 | grep -E "EXPORT|Error|Traceback" | grep -v EGL || true
rm -f "$out"/verify_*.png
ls "$out"
