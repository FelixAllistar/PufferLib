#!/usr/bin/env bash
set -euo pipefail
SWAT_SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [ -f "$SWAT_SCRIPT_DIR/config/swat.ini" ]; then
    SWAT_ROOT=$SWAT_SCRIPT_DIR # Installed as ./swat at the repository root.
else
    SWAT_ROOT=$(cd "$SWAT_SCRIPT_DIR/../.." && pwd)
fi
cd "$SWAT_ROOT"
SWAT_SERVER_MODE=0
if [ "${1:-}" = server ]; then SWAT_SERVER_MODE=1; shift; fi
SWAT_CHARACTER_MODE=0
if [ "${1:-}" = character ]; then SWAT_CHARACTER_MODE=1; shift; fi

swat_needs_build() {
    local binary=$1 source
    [ -f "$binary" ] || return 0
    for source in ocean/swat/*.c ocean/swat/*.h ocean/swat/generated/*.h ocean/swat/vendor/*.h ocean/swat/tests/character_probe.c ocean/swat/CMakeLists.txt ocean/swat/Makefile resources/shared/Roboto-Regular.ttf ocean/swat/build-windows.sh \
                  ocean/swat/assets/environment/* \
                  ocean/swat/assets/environment/materials_v1/*.png \
                  ocean/swat/assets/environment/lighting_v1/*.bin \
                  ocean/swat/assets/environment/motel_v1/*.glb \
                  ocean/swat/assets/environment/motel_room101_v2/*.glb \
                  ocean/swat/assets/environment/motel_room101_v3/*.glb \
                  ocean/swat/assets/environment/storefront_v1/*.glb \
                  ocean/swat/assets/characters/*.glb ocean/swat/assets/characters/upper_gear_f/*.glb \
                  ocean/swat/assets/characters/textures/*.png \
                  ocean/swat/assets/characters/*.json ocean/swat/assets/characters/upper_gear_f/*.json \
                  ocean/swat/assets/weapons/*.glb ocean/swat/assets/weapons/*.json \
                  ocean/swat/assets/ui/* \
                  ocean/swat/assets/audio/* \
                  config/swat.ini config/default.ini vendor/raygui.h \
                  vendor/enet/*.c vendor/enet/include/enet/*.h; do
        [ "$source" -nt "$binary" ] && return 0
    done
    return 1
}

# Win32 provides true relative input and cursor confinement on Windows. Keep
# WSLg for Linux development; use the native player for actual play from WSL.
if { [ -n "${WSL_INTEROP:-}" ] || [ -e /proc/sys/fs/binfmt_misc/WSLInterop ]; } && [ "${SWAT_NATIVE_WINDOWS:-1}" != 0 ]; then
    # Linux variables otherwise disappear across interop. Forward only supplied
    # game overrides; path entries are translated by WSL, existing rules kept.
    for SWAT_FORWARD in SWAT_SOUND_ASSETS/pw SWAT_LIGHTING/w SWAT_EXPOSURE/w SWAT_PLASTER_STYLE/w SWAT_ENVIRONMENT_STYLE/w SWAT_ENVIRONMENT_PBR/w SWAT_ENVIRONMENT_ART/w SWAT_WEAPON_ART/w SWAT_WEAPON_ASSETS/pw SWAT_CHARACTER_ART/w SWAT_CHARACTER_NORMALS/w SWAT_CHARACTER_ASSETS/pw \
                        SWAT_ENVIRONMENT_ASSETS/pw SWAT_MOTEL_ROOM101/w SWAT_IBL/w SWAT_CONTACT_SHADOWS/w SWAT_STEAM_AUDIO_LIBRARY/pw SWAT_HRTF_SOFA/pw; do
        SWAT_FORWARD_KEY=${SWAT_FORWARD%%/*}
        if [ -v "$SWAT_FORWARD_KEY" ]; then
            case ":${WSLENV:-}:" in
                *":$SWAT_FORWARD_KEY:"*|*":$SWAT_FORWARD_KEY/"*) ;;
                *) export WSLENV="${WSLENV:+$WSLENV:}$SWAT_FORWARD" ;;
            esac
        fi
    done
    SWAT_BUILD_TARGET=player
    SWAT_WINDOWS_BINARY=swat.exe
    if [ "$SWAT_SERVER_MODE" = 1 ]; then SWAT_WINDOWS_BINARY=swat-server.exe; SWAT_BUILD_TARGET=server; fi
    if [ "$SWAT_CHARACTER_MODE" = 1 ]; then SWAT_WINDOWS_BINARY=character_lab.exe; SWAT_BUILD_TARGET=character; fi
    if swat_needs_build "build/swat/windows/$SWAT_WINDOWS_BINARY"; then
        bash ocean/swat/build-windows.sh --target "$SWAT_BUILD_TARGET"
    fi
    SWAT_ARGS=()
    SWAT_PATH_NEXT=0
    for argument in "$@"; do
        if [ "$SWAT_PATH_NEXT" = 1 ]; then
            SWAT_ARGS+=("$(wslpath -aw "$argument")")
            SWAT_PATH_NEXT=0
        else
            SWAT_ARGS+=("$argument")
            case "$argument" in
                watch|--eval|--settings|--capture|--layout-model|--record|--resume|--asset|--locomotion-policy) SWAT_PATH_NEXT=1 ;;
            esac
        fi
    done
    printf "[swat] Native Windows player; SWAT_NATIVE_WINDOWS=0 selects WSLg.\n" >&2
    cd build/swat/windows
    exec "./$SWAT_WINDOWS_BINARY" "${SWAT_ARGS[@]}"
fi

if [ "$SWAT_SERVER_MODE" = 1 ]; then
    if swat_needs_build build/swat/server; then make -C ocean/swat server; fi
    exec ./build/swat/server "$@"
fi
# WSLg may select llvmpipe even with /dev/dxg available. Prefer its observed
# D3D12 hardware driver only when the user has not chosen a renderer explicitly.
if [ -e /dev/dxg ] && [ -z "${GALLIUM_DRIVER:-}" ] && [ -z "${MESA_LOADER_DRIVER_OVERRIDE:-}" ] && [ -z "${LIBGL_ALWAYS_SOFTWARE:-}" ]; then
    export GALLIUM_DRIVER=d3d12
fi
printf "[swat] Linux player; Gallium driver: %s (device appears in the GL startup log).\n" "${GALLIUM_DRIVER:-automatic}" >&2
if [ "$SWAT_CHARACTER_MODE" = 1 ]; then
    if swat_needs_build build/swat/character_lab; then make -C ocean/swat character-lab; fi
    exec ./build/swat/character_lab "$@"
fi
if swat_needs_build build/swat/swat; then make -C ocean/swat viewer; fi
exec ./build/swat/swat "$@"
