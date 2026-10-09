#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 source/make_textures.py
blender -b -t 4 --python source/build_asset.py > qa/build.log 2>&1
blender -b -t 2 --python source/audit_geometry.py > qa/geometry_audit.log 2>&1
python3 source/audit_glb.py
