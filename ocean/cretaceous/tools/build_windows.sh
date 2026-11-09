#!/usr/bin/env bash
# Native Windows build of the Cretaceous player (cretaceous.exe), cross-compiled
# from Linux/WSL with zig cc. Under WSL the Linux player cannot lock the
# pointer; the Windows exe uses real mouse capture.
#
#   ocean/cretaceous/tools/build_windows.sh [OUT_DIR]     (default build/cretaceous/win)
#
# Needs (nothing is installed system-wide):
#   ZIG=~/tools/zig-0.13.0/zig            the official Zig 0.13.0 tarball, used in place
#   RAYLIB_SRC=~/tools/raylib-5.5-src     the raysan5/raylib 5.5 release source tarball
#   BOX3D_DIR=../box3d                    the Box3D fork (same default as build.sh)
# raylib (PLATFORM_DESKTOP, GLFW, OpenGL 3.3 as in the Linux release build),
# Box3D and ENet are compiled from source once into CRET_WIN_DEPS
# (default build/cretaceous/win-deps) and rebuilt only when their sources or
# flags change. The game sources are those of `./build.sh cretaceous OUT --cpu`
# with the same flags and defines, minus OpenMP (zig ships no Windows OpenMP
# runtime, so those loops run on one thread; terra and the texture builder
# guard their pragmas with _OPENMP). The C runtime is linked statically: the
# exe needs only system DLLs. JOBS=N sets parallel compiles (default 4).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
OUT=${1:-$ROOT/build/cretaceous/win}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)   # relative to the caller's directory
cd "$ROOT"
ZIG=${ZIG:-$HOME/tools/zig-0.13.0/zig}
RAYLIB_SRC=${RAYLIB_SRC:-$HOME/tools/raylib-5.5-src}
BOX3D_DIR=${BOX3D_DIR:-../box3d}
DEPS=${CRET_WIN_DEPS:-build/cretaceous/win-deps}
JOBS=${JOBS:-4}
SRC=ocean/cretaceous

[ -x "$ZIG" ] || { echo "zig not found at $ZIG (set ZIG=)" >&2; exit 1; }
[ -f "$RAYLIB_SRC/src/raylib.h" ] || { echo "raylib 5.5 source not found at $RAYLIB_SRC (set RAYLIB_SRC=)" >&2; exit 1; }
[ -f "$BOX3D_DIR/include/box3d/box3d.h" ] || { echo "Box3D not found at $BOX3D_DIR (set BOX3D_DIR=)" >&2; exit 1; }
BOX3D_DIR=$(cd "$BOX3D_DIR" && pwd)
RAYLIB_SRC=$(cd "$RAYLIB_SRC" && pwd)
mkdir -p "$DEPS"
DEPS=$(cd "$DEPS" && pwd)

TARGET=x86_64-windows-gnu
CC=("$ZIG" cc -target "$TARGET")
# zig keeps its own compile cache; keep it next to the deps rather than in ~/.cache.
export ZIG_GLOBAL_CACHE_DIR=${ZIG_GLOBAL_CACHE_DIR:-$DEPS/zig-cache}
export ZIG_LOCAL_CACHE_DIR=${ZIG_LOCAL_CACHE_DIR:-$DEPS/zig-cache}

# compile_all OBJDIR "FLAGS..." SRC...: one object per source, JOBS at a time.
compile_all() {
    local objdir=$1 flags=$2; shift 2
    mkdir -p "$objdir"
    local pids=() src obj
    for src in "$@"; do
        obj="$objdir/$(echo "${src#"$ROOT"/}" | tr '/' '_' | sed 's/\.c$/.o/')"
        # shellcheck disable=SC2086
        "${CC[@]}" $flags -c "$src" -o "$obj" &
        pids+=($!)
        if [ "${#pids[@]}" -ge "$JOBS" ]; then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
    done
    local p; for p in "${pids[@]}"; do wait "$p"; done
}

# build_lib NAME "FLAGS" SRC...: static library, rebuilt when the sources or flags change.
build_lib() {
    local name=$1 flags=$2; shift 2
    local lib="$DEPS/lib$name.a" stamp
    stamp=$( { echo "$flags"; "$ZIG" version; cat "$@"; } | sha256sum | cut -c1-16)
    if [ -f "$lib" ] && [ "$(cat "$DEPS/$name.stamp" 2>/dev/null)" = "$stamp" ]; then return; fi
    echo "Building $name for Windows..."
    rm -rf "$DEPS/obj-$name"; rm -f "$lib"
    compile_all "$DEPS/obj-$name" "$flags" "$@"
    "$ZIG" ar rcs "$lib" "$DEPS/obj-$name"/*.o
    echo "$stamp" > "$DEPS/$name.stamp"
}

# raylib 5.5: the module list and defines of its src/Makefile for PLATFORM_DESKTOP
# (GLFW built in via rglfw.c, OpenGL 3.3 like the Linux release archive).
RAYLIB_FLAGS="-O2 -std=gnu99 -D_GNU_SOURCE -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 -Wno-missing-braces -fno-strict-aliasing -w -I$RAYLIB_SRC/src -I$RAYLIB_SRC/src/external/glfw/include"
build_lib raylib "$RAYLIB_FLAGS" \
    "$RAYLIB_SRC"/src/{rcore,rshapes,rtextures,rtext,rmodels,utils,raudio,rglfw}.c

# Box3D: the flags of its Release CMake build that build.sh links (libbox3d.a).
BOX3D_FLAGS="-O3 -DNDEBUG -std=gnu17 -ffp-contract=off -DB3_MAX_WORLDS=8192 -DBOX3D_VALIDATE -w -I$BOX3D_DIR/include -I$BOX3D_DIR/src"
mapfile -t BOX3D_SOURCES < <(ls "$BOX3D_DIR"/src/*.c)
build_lib box3d "$BOX3D_FLAGS" "${BOX3D_SOURCES[@]}"

# ENet (vendor/enet): the Win32 socket layer instead of unix.c.
ENET_FLAGS="-O2 -std=gnu99 -w -I$ROOT/vendor/enet/include"
build_lib enet "$ENET_FLAGS" "$ROOT"/vendor/enet/{callbacks,compress,host,list,packet,peer,protocol,win32}.c

# The player: build.sh's --cpu source list, flags and defines.
GAME_SOURCES=(
    $SRC/cretaceous.c
    $SRC/terra/terra.c $SRC/world.c $SRC/sim.c $SRC/abilities.c $SRC/progress.c $SRC/render/props_place.c
    $SRC/net.c $SRC/netclient.c $SRC/netsim.c
    $SRC/render/gfx.c $SRC/render/view.c $SRC/render/veg.c $SRC/render/props.c $SRC/render/csm.c $SRC/render/gprof.c $SRC/render/dino.c
    $SRC/hud.c $SRC/capture.c $SRC/audio.c $SRC/menu.c $SRC/character.c
    $SRC/win32/compat.c
)
GAME_FLAGS="-O2 -Wall -Wno-narrowing -Wno-unreachable-code -ferror-limit=3 -Werror=incompatible-pointer-types -Werror=return-type
    -Wno-error=incompatible-pointer-types-discards-qualifiers -Wno-incompatible-pointer-types-discards-qualifiers -Wno-error=array-parameter
    -Wno-unknown-pragmas -mavx2 -mfma
    -DPLATFORM_DESKTOP -DPUFFER_CRETACEOUS
    -I$SRC/win32 -include $SRC/win32/compat.h
    -I. -Isrc -I$SRC -Ivendor -I$RAYLIB_SRC/src -I$BOX3D_DIR/include -Ivendor/enet/include"
echo "Compiling cretaceous.exe..."
rm -rf "$DEPS/obj-game"
compile_all "$DEPS/obj-game" "$GAME_FLAGS" "${GAME_SOURCES[@]}"
for o in "$DEPS/obj-game"/*.o; do [ -s "$o" ] || { echo "compile failed" >&2; exit 1; }; done
[ "$(ls "$DEPS/obj-game"/*.o | wc -l)" = "${#GAME_SOURCES[@]}" ] || { echo "compile failed" >&2; exit 1; }

# Static CRT and deps; 16 MiB main-thread stack (Linux gives 8 MiB, Windows 1 MiB).
"${CC[@]}" -O2 -static "$DEPS/obj-game"/*.o \
    "$DEPS/libraylib.a" "$DEPS/libbox3d.a" "$DEPS/libenet.a" \
    -lopengl32 -lgdi32 -lwinmm -lws2_32 -lshell32 -luser32 -lkernel32 \
    -Wl,--stack,16777216 \
    -o "$OUT/cretaceous.exe"
rm -f "$OUT/cretaceous.pdb"
echo "Built: $OUT/cretaceous.exe"
