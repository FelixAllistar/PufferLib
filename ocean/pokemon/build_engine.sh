#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
PIN=9b88fd6c5467f703c38951d5b2e8a660314d410b
PK_BUILD="$(pwd)/build/pokemon"
PK_SOURCE="$PK_BUILD/engine-$PIN"
mkdir -p "$PK_BUILD"
if [ ! -d "$PK_SOURCE/src/lib" ]; then
    git clone --no-checkout https://github.com/pkmn/engine.git "$PK_SOURCE"
    git -C "$PK_SOURCE" checkout --detach "$PIN"
fi
if [ "$(git -C "$PK_SOURCE" rev-parse HEAD)" != "$PIN" ]; then
    echo "Unexpected engine revision in $PK_SOURCE" >&2
    exit 1
fi
PK_ZIG="${ZIG:-$PK_BUILD/zig-x86_64-linux-0.16.0/zig}"
if [ ! -x "$PK_ZIG" ]; then
    if [ "$(uname -sm)" != "Linux x86_64" ]; then
        echo "Set ZIG to a Zig 0.16.0 executable on this platform." >&2
        exit 1
    fi
    curl -fsSL --retry 3 https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz -o "$PK_BUILD/zig.tar.xz"
    printf '%s  %s\n' 70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00 "$PK_BUILD/zig.tar.xz" | sha256sum -c -
    tar -xJf "$PK_BUILD/zig.tar.xz" -C "$PK_BUILD"
fi
if [ "$("$PK_ZIG" version)" != "0.16.0" ]; then
    echo "This bridge is tested with Zig 0.16.0; set ZIG accordingly." >&2
    exit 1
fi
PK_OPT="${PK_OPT:-ReleaseFast}"
PK_ARGS=(-O "$PK_OPT" -lc --dep pkmn -Mroot=ocean/pokemon/bridge.zig
    -O "$PK_OPT" --dep build_options -Mpkmn="$PK_SOURCE/src/lib/pkmn.zig"
    -Mbuild_options=ocean/pokemon/build_options.zig)
if [ "${1:-}" = "test" ]; then
    "$PK_ZIG" test "${PK_ARGS[@]}"
else
    "$PK_ZIG" build-lib -fPIC -fcompiler-rt "${PK_ARGS[@]}" -femit-bin="$PK_BUILD/libpokemon.a"
fi
