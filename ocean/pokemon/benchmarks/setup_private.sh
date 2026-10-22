#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."
private_source=build/pokemon/metamon-eval
private_env=build/pokemon/metamon-eval-env
private_backend=${1:-cpu}
case "$private_backend" in
    cpu) private_torch='torch==2.8.0+cpu' ;;
    cu128) private_torch='torch==2.8.0+cu128'; private_env=build/pokemon/metamon-eval-env-cu128 ;;
    *) echo 'Usage: bash ocean/pokemon/benchmarks/setup_private.sh [cpu|cu128]' >&2; exit 2 ;;
esac
private_revision=0a00a759c9a4382a2877088d828302ec294a05a5
if [[ ! -d "$private_source/.git" ]]; then
    git clone --filter=blob:none --no-checkout https://github.com/UT-Austin-RPL/metamon.git "$private_source"
    git -C "$private_source" fetch --depth 1 origin "$private_revision"
    git -C "$private_source" checkout --detach "$private_revision"
fi
if [[ "$(git -C "$private_source" rev-parse HEAD)" != "$private_revision" ]]; then
    echo 'Unexpected Metamon revision; preserve the checkout and inspect it.' >&2
    exit 1
fi
if [[ ! -x "$private_env/bin/python" ]]; then
    uv venv --python 3.12 "$private_env"
fi
# Use a separate environment for each backend. Never install evaluation
# packages into the live trainer's environment. No training binary/config changes.
uv pip install --python "$private_env/bin/python" "$private_torch" \
    --index-url "https://download.pytorch.org/whl/$private_backend"
uv pip install --python "$private_env/bin/python" -e "$private_source" \
    "$private_torch" --extra-index-url "https://download.pytorch.org/whl/$private_backend" \
    --index-strategy unsafe-best-match
if [[ ! -f build/pokemon/libpokemon.a ]]; then
    make -C ocean/pokemon engine
fi
clang -O2 -g -std=c11 -Wall -Wextra -Wno-unused-function -Isrc \
    -Iraylib-5.5_linux_amd64/include -shared -fPIC \
    ocean/pokemon/benchmarks/private_adapter.c build/pokemon/libpokemon.a \
    raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lrt -lm \
    -o build/pokemon/private_adapter.so
clang -O2 -g -std=c11 -Wall -Wextra -Wno-unused-function -Isrc \
    -Iraylib-5.5_linux_amd64/include ocean/pokemon/tests/test_private_adapter.c \
    build/pokemon/libpokemon.a raylib-5.5_linux_amd64/lib/libraylib.a \
    -lGL -lpthread -ldl -lrt -lm -o build/pokemon/test_private_adapter
./build/pokemon/test_private_adapter
echo 'Ready: set METAMON_CACHE_DIR to an evaluation-only directory before running TaurosV0.'
