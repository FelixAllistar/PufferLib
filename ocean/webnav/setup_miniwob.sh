#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
revision=33c3b4ddef8c6eb67c57a29663d844b1eda7e614
checksum=dffc87414e5081b282fc54aefad7c3f4406f3a10dcb5923dfd9babdcf9aa60e9
archive=build/webnav/reference/miniwob.tar.gz
mkdir -p build/webnav/reference
if ! printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status 2>/dev/null; then
    curl -fL --retry 2 "https://codeload.github.com/Farama-Foundation/miniwob-plusplus/tar.gz/$revision" -o "$archive.download"
    printf '%s  %s\n' "$checksum" "$archive.download" | sha256sum --check
    mv "$archive.download" "$archive"
fi
if [ ! -f "build/webnav/reference/MiniWoB-plusplus-$revision/LICENSE" ]; then
    tar -xzf "$archive" -C build/webnav/reference
fi
printf 'MiniWoB reference: %s\n' "$revision"
