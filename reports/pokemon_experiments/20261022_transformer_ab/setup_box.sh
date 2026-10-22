#!/usr/bin/env bash
# Set up a fresh Vast box for the Transformer A/B. Run on the box from /workspace after copying
# pokemon_ab_bundle.tgz there (built locally; see README). The Metamon cache (models and panel teams,
# about 2 GB) is copied from the Pokemon box separately so both boxes score with identical files.
set -euo pipefail
cd /workspace
command -v clang >/dev/null && command -v ccache >/dev/null || (apt-get update -qq && apt-get install -y -qq clang ccache)
# Same NCCL as the Pokemon box (the trainer includes nccl.h even on one GPU).
[ -f /usr/include/nccl.h ] || (apt-get update -qq && apt-get install -y -qq libnccl2=2.32.3-1+cuda13.4 libnccl-dev=2.32.3-1+cuda13.4)
command -v uv >/dev/null || (curl -LsSf https://astral.sh/uv/install.sh | sh && ln -sf ~/.local/bin/uv /usr/local/bin/uv)
[ -f PufferLib/build.sh ] || (mkdir -p PufferLib && tar xzf pokemon_ab_bundle.tgz -C PufferLib)
cd PufferLib
./build.sh pokemon 2>&1 | tail -2
make -C ocean/pokemon encoder-test 2>&1 | tail -2
PK_ATTN_LAYERS=2 PK_ATTN_WIDTH=128 PK_ATTN_HEADS=4 ./build/pokemon/test_encoder | tail -2
bash ocean/pokemon/benchmarks/setup_private.sh cu128 2>&1 | tail -3
