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

swat_needs_build() {
    local binary=$1 source
    [ -f "$binary" ] || return 0
    for source in ocean/swat/*.c ocean/swat/*.h ocean/swat/generated/*.h resources/shared/Roboto-Regular.ttf ocean/swat/build-windows.sh \
                  ocean/swat/assets/environment/* \
                  config/swat.ini config/default.ini vendor/raygui.h \
                  vendor/enet/*.c vendor/enet/include/enet/*.h; do
        [ "$source" -nt "$binary" ] && return 0
    done
    return 1
}

# Win32 provides true relative input and cursor confinement on Windows. Keep
# WSLg for Linux development; use the native player for actual play from WSL.
if [ -n "${WSL_INTEROP:-}" ] && [ "${SWAT_NATIVE_WINDOWS:-1}" != 0 ]; then
    SWAT_WINDOWS_BINARY=swat.exe
    if [ "$SWAT_SERVER_MODE" = 1 ]; then SWAT_WINDOWS_BINARY=swat-server.exe; fi
    if swat_needs_build "build/swat/windows/$SWAT_WINDOWS_BINARY"; then
        bash ocean/swat/build-windows.sh
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
                watch|--eval|--settings|--capture|--layout-model|--record) SWAT_PATH_NEXT=1 ;;
            esac
        fi
    done
    cd build/swat/windows
    exec "./$SWAT_WINDOWS_BINARY" "${SWAT_ARGS[@]}"
fi

if [ "$SWAT_SERVER_MODE" = 1 ]; then
    if swat_needs_build build/swat/server; then make -C ocean/swat server; fi
    exec ./build/swat/server "$@"
fi
if swat_needs_build build/swat/swat; then make -C ocean/swat viewer; fi
exec ./build/swat/swat "$@"
