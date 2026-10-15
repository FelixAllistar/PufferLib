#!/usr/bin/env bash
set -euo pipefail

IW_SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
IW_ROOT=$(cd -- "$IW_SCRIPT_DIR/../.." && pwd)

# Keep the public executable name while running the compiled binary beside it.
# build.sh calls this after a successful Linux build, including custom outputs.
if [ "${1:-}" = --install ]; then
    IW_OUTPUT=$(realpath -- "$2")
    if [ "$(head -c 4 -- "$IW_OUTPUT")" != $'\177ELF' ]; then
        printf '[impulse_wars] Expected an ELF binary to wrap: %s\n' "$IW_OUTPUT" >&2
        exit 1
    fi
    IW_OUTPUT_DIR=$(dirname -- "$IW_OUTPUT")
    IW_ROOT_REL=$(realpath --relative-to="$IW_OUTPUT_DIR" -- "$IW_ROOT")
    IW_LAUNCHER=$(mktemp -- "$IW_OUTPUT.launcher.XXXXXX")
    trap 'rm -f -- "$IW_LAUNCHER"' EXIT
    {
        printf '#!/usr/bin/env bash\nset -euo pipefail\n'
        printf 'IW_LAUNCHER_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)\n'
        printf 'exec bash "$IW_LAUNCHER_DIR"/%q "$IW_LAUNCHER_DIR"/%q "$@"\n' \
            "$IW_ROOT_REL/ocean/impulse_wars/launch.sh" "$(basename -- "$IW_OUTPUT").elf"
    } > "$IW_LAUNCHER"
    chmod 755 "$IW_LAUNCHER"
    mv -f -- "$IW_OUTPUT" "$IW_OUTPUT.elf"
    mv -f -- "$IW_LAUNCHER" "$IW_OUTPUT"
    exit 0
fi

# Match SWAT's WSLg default: prefer hardware D3D12 over llvmpipe while
# preserving an explicit renderer choice.
if [ -e /dev/dxg ] && [ -z "${GALLIUM_DRIVER:-}" ] && [ -z "${MESA_LOADER_DRIVER_OVERRIDE:-}" ] && [ -z "${LIBGL_ALWAYS_SOFTWARE:-}" ]; then
    export GALLIUM_DRIVER=d3d12
fi

IW_BINARY=$1
shift
cd -- "$IW_ROOT"
exec "$IW_BINARY" "$@"
