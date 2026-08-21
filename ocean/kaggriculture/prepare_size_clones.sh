#!/usr/bin/env bash
# Train architecture variants from the corrected top-bot demonstrations.
set -euo pipefail
cd "$(cd "$(dirname "$0")/../.." && pwd)"

data_dir=saved/kaggriculture_bc_512x3_top
out_dir=saved/kaggriculture_bc_size_variants
mkdir -p "$out_dir"
bc=./ocean/kaggriculture/build/kag_bc

for spec in 256x2:256:2 512x2:512:2; do
    IFS=: read -r name hidden layers <<< "$spec"
    echo "=== $name opening anchor ==="
    "$bc" bc.mode=train \
      bc.data="$data_dir/opening_data.bin" bc.epochs=500 bc.batch=64 \
      bc.learning_rate=0.001 bc.opening_steps=26 bc.opening_weight=1 \
      bc.validation_games=80 bc.zero_reset_source=0 \
      policy.hidden_size="$hidden" policy.num_layers="$layers" \
      bc.output="$out_dir/opening_anchor_h${hidden}_l${layers}.bin"
    echo "=== $name recovery/top clone ==="
    "$bc" bc.mode=train \
      bc.data="$data_dir/recovery_96_data.bin" bc.epochs=1000 bc.batch=32 \
      bc.learning_rate=0.0002 bc.opening_steps=26 bc.opening_weight=3 \
      bc.validation_games=80 bc.zero_reset_source=0 \
      bc.load_model_path="$out_dir/opening_anchor_h${hidden}_l${layers}.bin" \
      policy.hidden_size="$hidden" policy.num_layers="$layers" \
      bc.output="$out_dir/top_clone_h${hidden}_l${layers}.bin"
done
echo "=== size clone preparation complete ==="
ls -lh "$out_dir"/*.bin
