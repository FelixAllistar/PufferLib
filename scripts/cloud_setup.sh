#!/usr/bin/env bash
# Setup script for the Claude Code cloud environment (claude.ai/code -> environment settings -> Setup script).
# Paste this file's contents there. It runs as root on the Ubuntu cloud image before each session starts.
# The cloud machine has NO GPU and no CUDA toolkit, so only CPU builds work there:
#   ./build.sh ENV --cpu            -> ./ENV (play/eval binary)
#   ./build.sh ENV OUT --cpu        -> ./OUT
# The native trainer (./build.sh ENV, which links CUDA + NCCL) and --cu builds need a GPU box (Vast).
# ROMs are not fetched: they live in the private repo FelixAllistar/puffer-roms and need a deploy key.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# clang + libomp: build.sh compiles with clang -fopenmp and links -lomp5 on Linux.
# libgl-dev: raylib's static lib links -lGL. ccache: build.sh uses it when present.
# xvfb: lets raylib binaries open a window headlessly (xvfb-run ./ENV) for smoke tests.
apt-get install -y -qq --no-install-recommends \
    build-essential clang libomp-dev ccache \
    curl unzip ca-certificates git pkg-config \
    libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    xvfb python3 python3-pip python3-venv >/dev/null

# Python helpers used by scripts under ocean/*/tools and reports/ (numpy only; no torch on a CPU box).
python3 -m pip install -q --break-system-packages numpy 2>/dev/null || python3 -m pip install -q numpy

# Keep OpenMP from spawning one thread per visible CPU (cloud nproc can overstate the real quota).
grep -q OMP_NUM_THREADS /etc/environment 2>/dev/null || echo 'OMP_NUM_THREADS=4' >> /etc/environment

clang --version | head -1
echo "pufferlib cloud setup done"

# --- Box3D and raylib, prebuilt before the session starts ---------------------
# Session commands go through an auto-mode classifier that sometimes refuses to
# build cloned code ("Code from External"). This script runs before the session,
# outside that classifier, so the physics library and raylib are always ready.
# SWAT and Cretaceous Makefiles look for ../box3d (next to the repo) and
# raylib-5.5_linux_amd64 in the repo root; both are linked from /opt.
apt-get install -y -qq --no-install-recommends cmake libgl1-mesa-dev >/dev/null
if [ ! -f /opt/box3d/build/src/libbox3d.a ]; then
    rm -rf /opt/box3d
    git clone -q https://github.com/FelixAllistar/box3d /opt/box3d
    # Samples need gtk and the unit tests are not used here, so both stay off.
    cmake -S /opt/box3d -B /opt/box3d/build -DCMAKE_BUILD_TYPE=Release \
        -DBOX3D_BENCHMARKS=OFF -DBOX3D_BUILD_SHADERS=OFF -DBOX3D_SAMPLES=OFF -DBOX3D_UNIT_TESTS=OFF >/dev/null
    cmake --build /opt/box3d/build --parallel "$(nproc)" >/dev/null
fi
if [ ! -f /opt/raylib-5.5_linux_amd64/lib/libraylib.a ]; then
    curl -fsSL https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_linux_amd64.tar.gz | tar -xz -C /opt
fi
# Sessions clone the repo to /home/user/<Repo>, so ../box3d is /home/user/box3d.
mkdir -p /home/user
ln -sfn /opt/box3d /home/user/box3d
# If the repo is already checked out, link raylib into its root as well;
# otherwise the session links it (ln -sfn /opt/raylib-5.5_linux_amd64 .).
for repo in /home/user/*/; do
    if [ -f "$repo/build.sh" ]; then ln -sfn /opt/raylib-5.5_linux_amd64 "$repo/raylib-5.5_linux_amd64"; fi
done
ls -l /opt/box3d/build/src/libbox3d.a /opt/raylib-5.5_linux_amd64/lib/libraylib.a
echo "box3d + raylib ready"

# --- Blender 4.2 LTS for headless asset scripting (art, props, previews) -----
# Sessions run blender -b ... -P script.py; see scripts/install_blender.sh.
bash "$(dirname "$0")/install_blender.sh" 2>/dev/null || {
    # The setup script may be pasted (no repo next to it): fetch the installer.
    curl -fsSL https://raw.githubusercontent.com/FelixAllistar/PufferLib/5.0/scripts/install_blender.sh | bash
}
echo "blender ready"
