#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$repo_root"
comparison_dir=build/webnav/encoder-comparison
mkdir -p "$comparison_dir/potion32" "$comparison_dir/minilm"

# The 8M baseline and the Rust tokenizer binding are produced by the native
# text test target. Bootstrap them when a fresh checkout does not have the
# ignored build artifacts yet; this keeps the comparison command reproducible
# without adding either dependency to the production build.
if [ ! -s build/webnav/reference/potion-tokenizer.json ] || \
   [ ! -s build/webnav/reference/potion-model.safetensors ] || \
   ! node -e "const p=require('./build/webnav/reference/tokenizers/node_modules/tokenizers/package.json'); if (p.version !== '0.23.2') process.exit(1)" 2>/dev/null; then
    make -f ocean/webnav/Makefile test-text
fi

download_if_missing() {
    local url=$1
    local target=$2
    if [ ! -s "$target" ]; then
        curl -fL --retry 3 --retry-delay 1 "$url" -o "$target"
    fi
}

potion32_revision=1e5a03f8eeb2c98b928fbbd846f22f816360919f
minilm_revision=1110a243fdf4706b3f48f1d95db1a4f5529b4d41
download_if_missing "https://huggingface.co/minishlab/potion-base-32M/resolve/$potion32_revision/config.json" "$comparison_dir/potion32/config.json"
download_if_missing "https://huggingface.co/minishlab/potion-base-32M/resolve/$potion32_revision/tokenizer.json" "$comparison_dir/potion32/tokenizer.json"
download_if_missing "https://huggingface.co/minishlab/potion-base-32M/resolve/$potion32_revision/model.safetensors" "$comparison_dir/potion32/model.safetensors"
download_if_missing "https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/$minilm_revision/config.json" "$comparison_dir/minilm/config.json"
download_if_missing "https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/$minilm_revision/tokenizer.json" "$comparison_dir/minilm/tokenizer.json"
download_if_missing "https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/$minilm_revision/onnx/model_quint8_avx2.onnx" "$comparison_dir/minilm/onnx-model.onnx"

if ! node -e "require('./$comparison_dir/node_modules/onnxruntime-node')" >/dev/null 2>&1; then
    npm install --prefix "$comparison_dir" --save-exact --no-audit --no-fund onnxruntime-node@1.22.0
fi

cat <<'EOF' | sha256sum -c -
e67e803f624fb4d67dea1c730d06e1067e1b14d830e2c2202569e3ef0f70bb50  build/webnav/reference/potion-tokenizer.json
f65d0f325faadc1e121c319e2faa41170d3fa07d8c89abd48ca5358d9a223de2  build/webnav/reference/potion-model.safetensors
63c00d90824c832c04ec1d02b6a983fb90489bf049f29fbff15ba481b8a432ee  build/webnav/encoder-comparison/potion32/config.json
99f6c33204c9231a7391871b7a3c91409b532c8f587a9ea44fc282303d8dec28  build/webnav/encoder-comparison/potion32/model.safetensors
7d75cbc54318138807c401b0f0c9721117c628b39de8e8e0edb6cb17e0ee7d18  build/webnav/encoder-comparison/potion32/tokenizer.json
953f9c0d463486b10a6871cc2fd59f223b2c70184f49815e7efbcab5d8908b41  build/webnav/encoder-comparison/minilm/config.json
b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21  build/webnav/encoder-comparison/minilm/onnx-model.onnx
be50c3628f2bf5bb5e3a7f17b1f74611b2561a3a27eeab05e5aa30f411572037  build/webnav/encoder-comparison/minilm/tokenizer.json
EOF
sha256sum build/webnav/reference/potion-tokenizer.json build/webnav/reference/potion-model.safetensors \
    "$comparison_dir"/potion32/* "$comparison_dir"/minilm/* > "$comparison_dir/SHA256SUMS"
node ocean/webnav/tools/encoder_compare.cjs
