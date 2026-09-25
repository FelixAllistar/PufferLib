#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p build/webnav
bash ocean/webnav/tools/text_probe.sh > build/webnav/text-probe.json
# These are reference-generation tools only; runtime/training remains native C.
if [ ! -f build/webnav/reference/tokenizers/node_modules/tokenizers/tokenizers.linux-x64-gnu.node ]; then
    npm install --prefix build/webnav/reference/tokenizers --save-exact --ignore-scripts --no-audit --no-fund tokenizers@0.23.2
fi
node ocean/webnav/tools/tokenizer_reference.cjs
clang-19 -O3 -std=c11 -Ivendor -Iocean/webnav ocean/webnav/tests/test_text_encoder.c \
    ocean/webnav/text_encoder.c vendor/cJSON.c -licuuc -lm -o build/webnav/test_text_encoder
build/webnav/test_text_encoder
clang-19 -O3 -std=c11 -Ivendor -Iocean/webnav ocean/webnav/tests/test_text_cache.c \
    ocean/webnav/text_encoder.c ocean/webnav/text_cache.c vendor/cJSON.c -licuuc -lm -o build/webnav/test_text_cache
build/webnav/test_text_cache
