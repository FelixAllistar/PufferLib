#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bend_bin="${BEND_BIN:-$HOME/.bend/bin/bend}"
version=$(BEND_NO_TELEMETRY=1 "$bend_bin" --version)
if [ "$version" != 'bend 2.0.6' ]; then
    echo "webnav bridge requires stock bend 2.0.6; got: $version" >&2
    exit 1
fi
mkdir -p build/webnav
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/bend/PROOF.bend
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/bend/Main.bend -o build/webnav/webnav_generated.c
clang-19 -O3 -std=c11 -fPIC -Ibuild/webnav -Iocean/webnav \
    -c ocean/webnav/bridge.c -o build/webnav/bridge.o
ar rcs build/webnav/libwebnav.a build/webnav/bridge.o
printf '%s\n' "$version" > build/webnav/compiler-version.txt
sha256sum build/webnav/webnav_generated.c ocean/webnav/bend/*.bend \
    ocean/webnav/bend/*.c ocean/webnav/bridge.c ocean/webnav/bridge.h \
    ocean/webnav/observation.h > build/webnav/sources.sha256
