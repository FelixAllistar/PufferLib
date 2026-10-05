#!/usr/bin/env bash
set -euo pipefail
SWAT_TARGET=all
if [ "$#" != 0 ]; then
    if [ "$#" != 2 ] || [ "$1" != --target ]; then
        echo "Usage: build-windows.sh [--target all|player|server|character]" >&2
        exit 2
    fi
    SWAT_TARGET=$2
fi
case "$SWAT_TARGET" in all|player|server|character) ;; *) echo "Unknown build target: $SWAT_TARGET" >&2; exit 2 ;; esac

# Build the same player against native Win32 Raylib, avoiding WSLg's RDP mouse
# path. Missing cross-tools are unpacked locally; no sudo/system install.
SWAT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SWAT_BUILD="$SWAT_ROOT/build/swat/windows"
SWAT_DEPS="$SWAT_ROOT/build/swat/windows-deps"
SWAT_BOX3D=${BOX3D_DIR:-"$SWAT_ROOT/../box3d"}
mkdir -p "$SWAT_BUILD" "$SWAT_DEPS"

if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    SWAT_CC=$(command -v x86_64-w64-mingw32-gcc)
    SWAT_CXX=$(command -v x86_64-w64-mingw32-g++)
else
    SWAT_TOOLS="$SWAT_DEPS/toolchain"
    SWAT_CC="$SWAT_TOOLS/usr/bin/x86_64-w64-mingw32-gcc-posix"
    SWAT_CXX="$SWAT_TOOLS/usr/bin/x86_64-w64-mingw32-g++-posix"
    if [ ! -x "$SWAT_CC" ] || [ ! -x "$SWAT_CXX" ]; then
        command -v apt-get >/dev/null || {
            echo "Install the MinGW-w64 C/C++ cross-compilers, then rerun this script." >&2
            exit 1
        }
        mkdir -p "$SWAT_DEPS/packages" "$SWAT_TOOLS"
        (
            cd "$SWAT_DEPS/packages"
            apt-get download binutils-mingw-w64-x86-64 mingw-w64-common \
                mingw-w64-x86-64-dev gcc-mingw-w64-base \
                gcc-mingw-w64-x86-64-posix-runtime \
                gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix
        )
        for package in "$SWAT_DEPS/packages/"*.deb; do
            dpkg-deb -x "$package" "$SWAT_TOOLS"
        done
    fi
fi
export PATH="$(dirname "$SWAT_CC"):$PATH"

SWAT_RAYLIB="$SWAT_DEPS/raylib-5.5_win64_mingw-w64"
if [ ! -f "$SWAT_RAYLIB/lib/libraylib.a" ]; then
    curl -fL --retry 2 \
        https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_win64_mingw-w64.zip \
        -o "$SWAT_DEPS/raylib-5.5_win64_mingw-w64.zip"
    unzip -q -o "$SWAT_DEPS/raylib-5.5_win64_mingw-w64.zip" -d "$SWAT_DEPS"
fi

cat > "$SWAT_BUILD/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER "$SWAT_CC")
set(CMAKE_CXX_COMPILER "$SWAT_CXX")
set(CMAKE_RC_COMPILER "$(dirname "$SWAT_CC")/x86_64-w64-mingw32-windres")
EOF

cmake -S "$SWAT_BOX3D" -B "$SWAT_BUILD/box3d" \
    -DCMAKE_TOOLCHAIN_FILE="$SWAT_BUILD/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF \
    -DBOX3D_SAMPLES=OFF -DBOX3D_UNIT_TESTS=OFF -DBOX3D_BENCHMARKS=OFF
if ! cmake --build "$SWAT_BUILD/box3d" --parallel 4 > "$SWAT_BUILD/box3d-build.log" 2>&1; then
    tail -n 60 "$SWAT_BUILD/box3d-build.log" >&2
    exit 1
fi

cmake -S "$SWAT_ROOT/vendor/enet" -B "$SWAT_BUILD/enet" \
    -DCMAKE_TOOLCHAIN_FILE="$SWAT_BUILD/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
if ! cmake --build "$SWAT_BUILD/enet" --parallel 4 > "$SWAT_BUILD/enet-build.log" 2>&1; then
    tail -n 60 "$SWAT_BUILD/enet-build.log" >&2
    exit 1
fi

SWAT_CORE=(body.c controller.c pose.c devices.c encounter.c weapons.c materials.c world.c motel.c storefront.c mission.c equipment.c tactical.c overwatch.c generation.c acoustics.c audio_dsp.c sim.c)
SWAT_SOURCES=()
SWAT_FLAGS=(-O2 -g -std=gnu11 -ffp-contract=off -Wall -Wextra
    -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas
    -I"$SWAT_ROOT/src" -I"$SWAT_ROOT/vendor" -I"$SWAT_ROOT/vendor/enet/include" -I"$SWAT_ROOT/ocean/swat"
    -I"$SWAT_BOX3D/include" -I"$SWAT_RAYLIB/include")
SWAT_HEADLESS_LIBS=("$SWAT_BUILD/box3d/src/libbox3d.a" "$SWAT_BUILD/enet/libenet.a" -static -lws2_32 -lwinmm -lm)
SWAT_LIBS=("$SWAT_BUILD/box3d/src/libbox3d.a" "$SWAT_BUILD/enet/libenet.a" "$SWAT_RAYLIB/lib/libraylib.a"
    -static -lopengl32 -lgdi32 -lws2_32 -lwinmm -lm)
SWAT_NET=()
if [ "$SWAT_TARGET" = all ] || [ "$SWAT_TARGET" = character ]; then
"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/character_probe.c" \
    "$SWAT_ROOT/ocean/swat/character_asset.c" -static -lm -o "$SWAT_BUILD/character_probe.exe"
"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/character_lab.c" \
    "$SWAT_ROOT/ocean/swat/character_view.c" "$SWAT_ROOT/ocean/swat/character_asset.c" \
    "$SWAT_ROOT/ocean/swat/lighting.c" "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/character_lab.exe"
fi
# Compile the growing shared simulation once for the player, server and checks.
# Each invocation refreshes objects with the same flags; no stale header cache.
mkdir -p "$SWAT_BUILD/objects"
if [ "$SWAT_TARGET" != character ]; then
for source in "${SWAT_CORE[@]}" protocol.c net.c replay.c; do
    object="$SWAT_BUILD/objects/${source%.c}.o"
    "$SWAT_CC" "${SWAT_FLAGS[@]}" -c "$SWAT_ROOT/ocean/swat/$source" -o "$object"
    case "$source" in
        protocol.c|net.c|replay.c) SWAT_NET+=("$object") ;;
        *) SWAT_SOURCES+=("$object") ;;
    esac
done

fi

if [ "$SWAT_TARGET" = all ] || [ "$SWAT_TARGET" = player ]; then
"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/swat.c" \
    "$SWAT_ROOT/ocean/swat/character_runtime.c" "$SWAT_ROOT/ocean/swat/character_view.c" "$SWAT_ROOT/ocean/swat/character_asset.c" \
    "$SWAT_ROOT/ocean/swat/render.c" "$SWAT_ROOT/ocean/swat/lighting.c" "$SWAT_ROOT/ocean/swat/weapon_art.c" "$SWAT_ROOT/ocean/swat/environment_art.c" "$SWAT_ROOT/ocean/swat/frontend.c" \
    "$SWAT_ROOT/ocean/swat/settings.c" "$SWAT_ROOT/ocean/swat/feedback.c" "$SWAT_ROOT/ocean/swat/sound_view.c" "$SWAT_ROOT/ocean/swat/spatial_audio.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" \
    "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/swat.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_character_render.c" \
    "$SWAT_ROOT/ocean/swat/character_runtime.c" "$SWAT_ROOT/ocean/swat/character_view.c" "$SWAT_ROOT/ocean/swat/character_asset.c" \
    "$SWAT_ROOT/ocean/swat/lighting.c" "$SWAT_ROOT/ocean/swat/weapon_art.c" "$SWAT_ROOT/ocean/swat/environment_art.c" \
    "${SWAT_SOURCES[@]}" "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/test_character_render.exe"

fi

if [ "$SWAT_TARGET" = all ] || [ "$SWAT_TARGET" = server ]; then
"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/server.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/swat-server.exe"

fi

if [ "$SWAT_TARGET" = all ]; then
"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/replay_tool.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/replay_tool.exe"
"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_foundation.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_foundation.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_encounter.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_encounter.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_devices.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_devices.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_payloads.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_payloads.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/net_probe.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/net_probe.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_net.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_net.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_acoustics.c" \
    "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_acoustics.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_audio_dsp.c" \
    "$SWAT_ROOT/ocean/swat/audio_dsp.c" "$SWAT_ROOT/ocean/swat/materials.c" -static -lm -o "$SWAT_BUILD/test_audio_dsp.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_spatial_audio.c" \
    "$SWAT_ROOT/ocean/swat/spatial_audio.c" -static -lm -o "$SWAT_BUILD/test_spatial_audio.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_mission.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_mission.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_tactical.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_tactical.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_overwatch.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_overwatch.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/layout_tool.c" \
    "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/layout_tool.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_generation.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_generation.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/audio_lab.c" \
    "$SWAT_ROOT/ocean/swat/spatial_audio.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/audio_lab.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_protocol.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_protocol.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_sim.c" \
    "${SWAT_SOURCES[@]}" "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/test_sim.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_settings.c" \
    "$SWAT_ROOT/ocean/swat/settings.c" -static -lm -o "$SWAT_BUILD/test_settings.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_frontend.c" \
    "$SWAT_ROOT/ocean/swat/character_runtime.c" "$SWAT_ROOT/ocean/swat/character_view.c" "$SWAT_ROOT/ocean/swat/character_asset.c" \
    "$SWAT_ROOT/ocean/swat/render.c" "$SWAT_ROOT/ocean/swat/lighting.c" "$SWAT_ROOT/ocean/swat/weapon_art.c" "$SWAT_ROOT/ocean/swat/environment_art.c" "$SWAT_ROOT/ocean/swat/frontend.c" \
    "$SWAT_ROOT/ocean/swat/settings.c" "$SWAT_ROOT/ocean/swat/feedback.c" "$SWAT_ROOT/ocean/swat/sound_view.c" "$SWAT_ROOT/ocean/swat/spatial_audio.c" "${SWAT_SOURCES[@]}" \
    "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/test_frontend.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_motel.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_motel.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_storefront.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_storefront.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_environment_binding.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_environment_binding.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_environment_props.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_HEADLESS_LIBS[@]}" -o "$SWAT_BUILD/test_environment_props.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_environment_art.c" \
    "$SWAT_ROOT/ocean/swat/character_runtime.c" "$SWAT_ROOT/ocean/swat/character_view.c" "$SWAT_ROOT/ocean/swat/character_asset.c" \
    "$SWAT_ROOT/ocean/swat/render.c" "$SWAT_ROOT/ocean/swat/lighting.c" "$SWAT_ROOT/ocean/swat/weapon_art.c" "$SWAT_ROOT/ocean/swat/environment_art.c" \
    "$SWAT_ROOT/ocean/swat/protocol.c" "${SWAT_SOURCES[@]}" "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/test_environment_art.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_weapon_art.c" \
    "$SWAT_ROOT/ocean/swat/weapon_art.c" "$SWAT_ROOT/ocean/swat/lighting.c" "$SWAT_ROOT/ocean/swat/environment_art.c" \
    "${SWAT_SOURCES[@]}" "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/test_weapon_art.exe"

"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/tests/test_lighting.c" \
    "$SWAT_ROOT/ocean/swat/lighting.c" "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/test_lighting.exe"
"$SWAT_CC" "${SWAT_FLAGS[@]}" "$SWAT_ROOT/ocean/swat/performance_tool.c" \
    "$SWAT_ROOT/ocean/swat/character_runtime.c" "$SWAT_ROOT/ocean/swat/character_view.c" "$SWAT_ROOT/ocean/swat/character_asset.c" \
    "$SWAT_ROOT/ocean/swat/render.c" "$SWAT_ROOT/ocean/swat/lighting.c" "$SWAT_ROOT/ocean/swat/weapon_art.c" "$SWAT_ROOT/ocean/swat/environment_art.c" \
    "$SWAT_ROOT/ocean/swat/sound_view.c" "$SWAT_ROOT/ocean/swat/spatial_audio.c" \
    "${SWAT_NET[@]}" "${SWAT_SOURCES[@]}" "${SWAT_LIBS[@]}" -o "$SWAT_BUILD/performance_tool.exe"

fi

mkdir -p "$SWAT_BUILD/config"
cp "$SWAT_ROOT/config/default.ini" "$SWAT_ROOT/config/swat.ini" "$SWAT_BUILD/config/"
mkdir -p "$SWAT_BUILD/assets/ui"
cp "$SWAT_ROOT/resources/shared/Roboto-Regular.ttf" "$SWAT_BUILD/assets/ui/"
mkdir -p "$SWAT_BUILD/assets/environment"
cp "$SWAT_ROOT/ocean/swat/assets/environment/"*.{png,glb,json,txt} "$SWAT_BUILD/assets/environment/"
cp -R "$SWAT_ROOT/ocean/swat/assets/environment/materials_v1" "$SWAT_BUILD/assets/environment/"
cp -R "$SWAT_ROOT/ocean/swat/assets/environment/motel_v1" "$SWAT_BUILD/assets/environment/"
cp -R "$SWAT_ROOT/ocean/swat/assets/environment/storefront_v1" "$SWAT_BUILD/assets/environment/"
if [ -d "$SWAT_ROOT/build/swat/assets/characters" ]; then
    mkdir -p "$SWAT_BUILD/assets/characters"
    cp -R "$SWAT_ROOT/build/swat/assets/characters/." "$SWAT_BUILD/assets/characters/"
fi
SWAT_PRIVATE_RIFLE="$SWAT_ROOT/build/swat/assets/weapons/rifle7_rigid_textured.glb"
if [ -f "$SWAT_PRIVATE_RIFLE" ]; then
    mkdir -p "$SWAT_BUILD/assets/weapons"
    cp "$SWAT_PRIVATE_RIFLE" "$SWAT_BUILD/assets/weapons/"
fi
SWAT_PHONON="$SWAT_ROOT/build/swat/deps/steam-audio/steamaudio/lib/windows-x64/phonon.dll"
if [ -f "$SWAT_PHONON" ]; then
    cp "$SWAT_PHONON" "$SWAT_BUILD/phonon.dll"
    cp "$SWAT_ROOT/vendor/steam_audio/LICENSE.md" "$SWAT_BUILD/STEAM_AUDIO_LICENSE.md"
    cp "$SWAT_ROOT/vendor/steam_audio/THIRDPARTY.md" "$SWAT_BUILD/STEAM_AUDIO_THIRDPARTY.md"
fi
cat > "$SWAT_BUILD/Play SWAT.cmd" <<'EOF'
@echo off
pushd "%~dp0"
swat.exe play
popd
EOF
printf 'Built native Windows target: %s (%s)\n' "$SWAT_TARGET" "$SWAT_BUILD"
