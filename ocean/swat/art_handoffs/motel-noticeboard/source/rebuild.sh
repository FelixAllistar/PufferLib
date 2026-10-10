#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python make_atlas.py
blender -b -t 2 --python build_asset.py
python audit_glb.py
python audit_anchors.py
blender -b -t 2 --python audit_source.py
blender -b -t 3 --python render_glb.py
