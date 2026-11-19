#!/bin/sh
# Rebuild the Briar Court exterior kit end to end (deterministic).
#   sh ocean/swat/tools/blender/build_motel_exterior.sh [SOURCE_CACHE] [WORK_DIR]
# 1. motel_exterior_textures.py: download/verify CC0 Poly Haven maps into
#    SOURCE_CACHE and bake the tiled PBR library into WORK_DIR.
# 2. motel_exterior_kit.py: build geometry/materials, export
#    assets/environment/motel_exterior/{exterior_kit.glb,placements.json}.
# 3. tools/import_motel_exterior.py: regenerate motel_exterior_data.h.
# 4. motel_exterior_sections (make motel-exterior-sections): dump the
#    authoritative wall sections to motel_exterior/sections.json.
# 5. motel_exterior_arch.py: architecture overlay architecture_kit.glb +
#    architecture_placements.json; import_motel_exterior.py --arch writes
#    motel_exterior_arch_data.h.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
swat=$(cd "$here/../.." && pwd)
src=${1:-${TMPDIR:-/tmp}/swat-motel-exterior-src}
work=${2:-${TMPDIR:-/tmp}/swat-motel-exterior-tex}
blender=${BLENDER:-blender}
"$blender" -b --factory-startup --python-exit-code 1 -P "$here/motel_exterior_textures.py" -- "$src" "$work"
"$blender" -b --factory-startup --python-exit-code 1 -P "$here/motel_exterior_kit.py" -- "$work" "$swat/assets/environment/motel_exterior"
python3 "$swat/tools/import_motel_exterior.py"
make -C "$swat" motel-exterior-sections
"$swat/../../build/swat/motel_exterior_sections" > "$swat/assets/environment/motel_exterior/sections.json"
"$blender" -b --factory-startup --python-exit-code 1 -P "$here/motel_exterior_arch.py" -- "$swat/assets/environment/motel_exterior"
python3 "$swat/tools/import_motel_exterior.py" --arch
