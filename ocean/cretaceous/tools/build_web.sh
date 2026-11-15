#!/usr/bin/env bash
# Single-player web build of Cretaceous: WebAssembly + WebGL2, built with
# Emscripten. Solo play and "Play a fight" only; networking, ENet and the
# trainer are compiled out (net_web.c stands in for net.c).
#
#   ocean/cretaceous/tools/build_web.sh [OUT_DIR]     (default build/cretaceous/web)
#
# Writes OUT_DIR/cretaceous.js, OUT_DIR/cretaceous.wasm.bin (the wasm,
# gzipped) and the page (index.html, loader.js). The game data is packed separately by tools/package_web.sh,
# which runs this script first unless told otherwise.
#
# Needs (nothing is installed system-wide):
#   EMSDK=~/emsdk                      emsdk with an activated toolchain (source emsdk_env.sh is done here)
#   RAYLIB_WEB=raylib-5.5_webassembly  the raylib 5.5 release archive raylib-5.5_webassembly.zip, unpacked
#                                      in the repository root (OpenGL ES 2 build of raylib; this game draws
#                                      its 3D with its own WebGL2 code and uses raylib for the window, input,
#                                      2D, fonts and audio)
#   BOX3D_DIR=../box3d                 the Box3D fork; its wasm static library is built once with emcmake
#                                      into BOX3D_DIR/build-web (single-threaded: see below)
# Hosting is static files with no COOP/COEP headers, so there is no
# SharedArrayBuffer: the build is single-threaded. OpenMP is not enabled (its
# pragmas are ignored and the omp calls are behind _OPENMP), and the few
# pthread_create calls fail over to running inline, as they already do when a
# thread cannot start. The main loops stay as they are: ASYNCIFY lets
# WindowShouldClose (cretaceous.c) yield to the browser once per frame.
# ASYNCIFY_IGNORE_INDIRECT: only functions with a direct call path to that
# yield (main and the dev-mode loops) are rewritten to unwind and resume; by
# default every function that calls through a pointer was too (Box3D's
# solvers, qsort, the GL wrappers), which made the wasm 2.11 MB instead of
# 1.73 MB and slowed all of that code. No yield is reached through a pointer.
# JOBS=N sets parallel compiles (default 4). DEBUG=1 builds -O1 with assertions.
# PROFILE=1 keeps the wasm function names, so a browser's CPU profile names
# them (tools/web_smoke.js --perf --cpuprofile); the code is the same.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
OUT=${1:-$ROOT/build/cretaceous/web}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
cd "$ROOT"
EMSDK=${EMSDK:-$HOME/emsdk}
RAYLIB_WEB=${RAYLIB_WEB:-raylib-5.5_webassembly}
BOX3D_DIR=${BOX3D_DIR:-../box3d}
JOBS=${JOBS:-4}
SRC=ocean/cretaceous
DEPS=build/cretaceous/web-deps

if ! command -v emcc >/dev/null 2>&1; then
    [ -f "$EMSDK/emsdk_env.sh" ] || { echo "emsdk not found at $EMSDK (set EMSDK=)" >&2; exit 1; }
    # shellcheck disable=SC1091
    source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
fi
[ -f "$RAYLIB_WEB/lib/libraylib.a" ] || { echo "raylib web build not found at $RAYLIB_WEB (unpack raylib-5.5_webassembly.zip in the repository root, or set RAYLIB_WEB=)" >&2; exit 1; }
[ -f "$BOX3D_DIR/include/box3d/box3d.h" ] || { echo "Box3D not found at $BOX3D_DIR (set BOX3D_DIR=)" >&2; exit 1; }
BOX3D_DIR=$(cd "$BOX3D_DIR" && pwd); RAYLIB_WEB=$(cd "$RAYLIB_WEB" && pwd)
mkdir -p "$DEPS"; DEPS=$(cd "$DEPS" && pwd)

# Box3D for wasm. Its own CMakeLists turns on wasm pthreads when it is the
# top-level project, so it is configured as a subproject of a two-line wrapper
# instead: same sources and SIMD flags (simd128 on the SSE2 path), no threads.
B3_BUILD=$BOX3D_DIR/build-web
if [ ! -f "$B3_BUILD/box3d/src/libbox3d.a" ] || [ -n "$(find "$BOX3D_DIR/src" "$BOX3D_DIR/include" -newer "$B3_BUILD/box3d/src/libbox3d.a" -name '*.[ch]' -print -quit)" ]; then
    echo "Building Box3D for wasm..."
    mkdir -p "$DEPS/box3d-wrap"
    printf 'cmake_minimum_required(VERSION 3.22)\nproject(box3d_web C CXX)\nadd_subdirectory(${BOX3D_DIR} box3d)\n' > "$DEPS/box3d-wrap/CMakeLists.txt"
    emcmake cmake -S "$DEPS/box3d-wrap" -B "$B3_BUILD" -DBOX3D_DIR="$BOX3D_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$B3_BUILD" --parallel "$JOBS" >/dev/null
fi
B3_LIB=$B3_BUILD/box3d/src/libbox3d.a

# The player: build.sh's --cpu source list without the ENet modules.
GAME_SOURCES=(
    $SRC/cretaceous.c
    $SRC/terra/terra.c $SRC/world.c $SRC/sim.c $SRC/abilities.c $SRC/progress.c $SRC/render/props_place.c
    $SRC/net_web.c
    $SRC/render/gfx.c $SRC/render/view.c $SRC/render/veg.c $SRC/render/props.c $SRC/render/csm.c $SRC/render/gprof.c $SRC/render/dino.c
    $SRC/hud.c $SRC/capture.c $SRC/audio.c $SRC/menu.c $SRC/character.c
)
# Release: -O3 with link-time optimisation across the game's files (the
# sim's tick ran 1.4 ms at -O2 and 0.7 to 0.85 ms with LTO for 256 animals,
# tests/bench.c under node; -O3 alone changed nothing). Box3D's library stays
# as CMake builds it: LTO there measured no faster.
if [ "${DEBUG:-0}" = 1 ]; then OPT="-O1 -g2"; LINK_DEBUG="-sASSERTIONS=1"; else OPT="-O3 -flto"; LINK_DEBUG=""; fi
if [ "${PROFILE:-0}" = 1 ]; then LINK_DEBUG="$LINK_DEBUG --profiling-funcs"; fi
# ENet's headers are still on the include path: net.h declares the API with its types.
GAME_FLAGS="$OPT -std=gnu11 -Wall -Wno-unknown-pragmas -Wno-unused-function -Wno-unused-but-set-variable -Wno-missing-braces
    -msimd128 -DPLATFORM_WEB
    -I. -Isrc -I$SRC -Ivendor -I$RAYLIB_WEB/include -I$BOX3D_DIR/include -Ivendor/enet/include"

OBJ=$DEPS/obj-game
rm -rf "$OBJ"; mkdir -p "$OBJ"
echo "Compiling Cretaceous for the web..."
pids=()
for s in "${GAME_SOURCES[@]}"; do
    o="$OBJ/$(echo "${s#"$SRC"/}" | tr '/' '_' | sed 's/\.c$/.o/')"
    # shellcheck disable=SC2086
    emcc $GAME_FLAGS -c "$s" -o "$o" &
    pids+=($!)
    if [ "${#pids[@]}" -ge "$JOBS" ]; then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
done
fail=0; for p in "${pids[@]}"; do wait "$p" || fail=1; done
[ "$(ls "$OBJ"/*.o 2>/dev/null | wc -l)" = "${#GAME_SOURCES[@]}" ] && [ $fail = 0 ] || { echo "compile failed" >&2; exit 1; }

# Link. WebGL2 only (the renderer needs ES 3.0: instancing, MRT, float and
# array textures); memory grows on demand up to 4 GB (the 2048^2 world alone
# is 177 MB); a 16 MB stack as in the Windows build; the data files are
# written into the in-memory file system by the page before main starts
# (web/loader.js), at the paths the native game opens. cret_web_save is
# exported for the page, which calls it when it goes away (cretaceous.c).
echo "Linking..."
# shellcheck disable=SC2086
emcc $OPT "$OBJ"/*.o "$RAYLIB_WEB/lib/libraylib.a" "$B3_LIB" -o "$DEPS/cretaceous.js" \
    -sUSE_GLFW=3 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sASYNCIFY -sASYNCIFY_IGNORE_INDIRECT=1 -sASYNCIFY_STACK_SIZE=1048576 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=4294967296 -sSTACK_SIZE=16777216 \
    -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0 -sENVIRONMENT=web -sINVOKE_RUN=1 \
    -sEXPORTED_RUNTIME_METHODS=FS,ENV,addRunDependency,removeRunDependency,HEAPF32 \
    -sEXPORTED_FUNCTIONS=_main,_cret_web_save \
    $LINK_DEBUG
# The page inflates the wasm itself (static hosting gives no Content-Encoding),
# so only the gzipped copy goes next to the page.
cp "$DEPS/cretaceous.js" "$OUT/"
gzip -9 -n -c "$DEPS/cretaceous.wasm" > "$OUT/cretaceous.wasm.bin"
cp "$SRC/web/index.html" "$SRC/web/loader.js" "$OUT/"
# Stamp the page so a browser's cached loader, wasm or data never outlives a
# new build: loader.js?v=N, and the loader adds the same ?v=N to what it fetches.
sed -i -E "s#src=\"loader\.js(\?v=[0-9]+)?\"#src=\"loader.js?v=$(date +%s)\"#" "$OUT/index.html"
ls -l "$DEPS/cretaceous.wasm" "$OUT/cretaceous.js" "$OUT/cretaceous.wasm.bin" | awk '{printf "  %-60s %7.2f MB\n", $NF, $5 / 1048576}'
echo "Built: $OUT (pack the data with ocean/cretaceous/tools/package_web.sh)"
