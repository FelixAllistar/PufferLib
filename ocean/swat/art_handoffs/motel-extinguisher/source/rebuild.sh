#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python "$ROOT/source/make_atlas.py"
blender -b -t 2 --python "$ROOT/source/build_asset.py"
python "$ROOT/source/audit_glb.py"
python "$ROOT/source/audit_anchors.py"
blender -b -t 2 --python "$ROOT/source/audit_source.py"
blender -b -t 4 --python "$ROOT/source/render_glb.py"
