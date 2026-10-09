#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 source/make_atlas.py
blender -b -t 2 --python source/build_asset.py
python3 source/audit_ground.py
blender -b -t 4 --python source/render_glb.py
