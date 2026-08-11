#!/usr/bin/env bash
set -euo pipefail

# Independent 25-epoch imitation perturbations. The seed changes only the
# episode-level split/shuffle; architecture, source policy, data, and optimizer
# stay fixed. Numeric filenames let eval_top_checkpoints.sh scan the directory.
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$root"

data=${KAG_BC_DATA:-saved/kaggriculture_bc_top_full_v2.bin}
init=${KAG_BC_INIT:-saved/kaggriculture_hall_of_fame/expL_sourcezero.bin}
out_dir=${KAG_BC_OUT_DIR:-saved/kaggriculture_bc_seed25}
epochs=${KAG_BC_EPOCHS:-25}
mkdir -p "$out_dir" logs/kaggriculture
printf 'seed\tepochs\tcheckpoint\n' > "$out_dir/manifest.tsv"

for seed in ${KAG_BC_SEEDS:-11 23 47 101 202 303 404 505 606 707 808 909}; do
    file=$(printf '%016d.bin' "$seed")
    output="$out_dir/$file"
    log="logs/kaggriculture/bc_seed${seed}_e${epochs}.log"
    echo "BC basin: seed=$seed epochs=$epochs output=$output"
    ./ocean/kaggriculture/build/kag_bc \
        bc.mode=train \
        "bc.data=$data" \
        "bc.epochs=$epochs" \
        bc.batch=8 \
        bc.learning_rate=0.00005 \
        "bc.seed=$seed" \
        bc.validation_games=40 \
        bc.report_interval=25 \
        bc.anchor_l2=0 \
        bc.zero_reset_source=1 \
        "bc.load_model_path=$init" \
        "bc.output=$output" \
        policy.hidden_size=128 \
        policy.num_layers=2 2>&1 | tee "$log"
    printf '%d\t%d\t%s\n' "$seed" "$epochs" "$output" \
        >> "$out_dir/manifest.tsv"
done
