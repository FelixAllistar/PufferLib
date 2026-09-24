#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p build/webnav/reference
revision=bf8b056651a2c21b8d2565580b8569da283cab23
fetch() {
    local remote=$1 local_name=$2 checksum=$3
    local target="build/webnav/reference/$local_name"
    if ! printf '%s  %s\n' "$checksum" "$target" | sha256sum --check --status 2>/dev/null; then
        curl -fL --retry 2 "https://huggingface.co/minishlab/potion-base-8M/resolve/$revision/$remote" -o "$target.download"
        printf '%s  %s\n' "$checksum" "$target.download" | sha256sum --check
        mv "$target.download" "$target"
    fi
}
fetch tokenizer.json potion-tokenizer.json e67e803f624fb4d67dea1c730d06e1067e1b14d830e2c2202569e3ef0f70bb50
fetch model.safetensors potion-model.safetensors f65d0f325faadc1e121c319e2faa41170d3fa07d8c89abd48ca5358d9a223de2
clang-19 -O3 -std=c11 -Ivendor ocean/webnav/tools/text_probe.c vendor/cJSON.c -lm -o build/webnav/text_probe
build/webnav/text_probe build/webnav/reference/potion-tokenizer.json \
    build/webnav/reference/potion-model.safetensors ocean/webnav/tests/text_cases.json
