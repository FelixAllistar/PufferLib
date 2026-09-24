#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
version=153.0.8010.52
checksum=944dc1eae654637fed4d57650198774f9c43b45f34e48febb84f43c541b5de76
mkdir -p build/webnav
archive=build/webnav/chromium.zip
package=chrome-headless-shell
if [ "${1:-headless}" = headed ]; then
    package=chrome
    archive=build/webnav/chrome-full.zip
    checksum=e66f66d4802a46d4a022667e668aa950e277cadbfbed4b3777915b47413a0ef9
fi
if ! printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status; then
    curl -fL --retry 2 "https://storage.googleapis.com/chrome-for-testing-public/$version/linux64/$package-linux64.zip" -o "$archive.download"
    printf '%s  %s\n' "$checksum" "$archive.download" | sha256sum --check
    mv "$archive.download" "$archive"
fi
unzip -q -o "$archive" -d build/webnav
"build/webnav/$package-linux64/$package" --version
