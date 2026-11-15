#!/usr/bin/env bash
# Installs portable Blender 4.2 LTS (Linux x64) for headless scripting and CPU
# rendering: /opt/blender/blender, also linked as /usr/local/bin/blender.
# Idempotent; safe to call from the cloud setup script or from a session.
# Usage: sudo bash scripts/install_blender.sh   (root in the cloud already)
# Headless use: blender -b --factory-startup --python-exit-code 1 -P script.py -- args
# Rendering without a GPU: Cycles with device CPU, or Workbench / EEVEE through
# the software GL in xvfb-run (slow). Prefer Cycles CPU at low samples + denoise.
set -euo pipefail
VERSION=4.2.3
DIR=/opt/blender-$VERSION
if [ ! -x "$DIR/blender" ]; then
    export DEBIAN_FRONTEND=noninteractive
    if command -v apt-get >/dev/null; then
        # Shared libraries the official Linux build loads even in background mode.
        apt-get install -y -qq --no-install-recommends xz-utils curl ca-certificates \
            libxi6 libxxf86vm1 libxfixes3 libxrender1 libxkbcommon0 libsm6 libice6 \
            libgl1 libegl1 libglu1-mesa libxext6 libx11-6 >/dev/null || true
    fi
    tmp=$(mktemp -d)
    curl -fsSL "https://download.blender.org/release/Blender4.2/blender-$VERSION-linux-x64.tar.xz" -o "$tmp/b.tar.xz"
    mkdir -p "$DIR"
    tar -xJf "$tmp/b.tar.xz" -C "$DIR" --strip-components=1
    rm -rf "$tmp"
fi
ln -sfn "$DIR" /opt/blender
ln -sfn /opt/blender/blender /usr/local/bin/blender
/opt/blender/blender -b --factory-startup --python-expr "import bpy; print('blender ok', bpy.app.version_string)" 2>/dev/null | grep "blender ok"
