#!/usr/bin/env bash
set -euo pipefail

# Refine the useful "pinch of imitation" region around the 25-epoch C1 peak.
# Every candidate starts from the same sanitized ExpL policy and uses the same
# episode-level train/validation split; only the number of BC epochs changes.
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$root"

data=${KAG_BC_DATA:-saved/kaggriculture_bc_top_full_v2.bin}
init=${KAG_BC_INIT:-saved/kaggriculture_hall_of_fame/expL_sourcezero.bin}
out_dir=${KAG_BC_OUT_DIR:-saved/kaggriculture_bc_dose}
log_prefix=${KAG_BC_LOG_PREFIX:-bc_full_top}
mkdir -p "$out_dir" logs/kaggriculture

for epochs in ${KAG_BC_EPOCHS:-15 20 30 35}; do
    output="$out_dir/full_top_e${epochs}.bin"
    log="logs/kaggriculture/${log_prefix}_e${epochs}.log"
    echo "BC dose: epochs=$epochs output=$output"
    ./ocean/kaggriculture/build/kag_bc \
        bc.mode=train \
        "bc.data=$data" \
        "bc.epochs=$epochs" \
        bc.batch=8 \
        bc.learning_rate=0.00005 \
        bc.seed=7 \
        bc.validation_games=40 \
        bc.report_interval=5 \
        bc.anchor_l2=0 \
        bc.zero_reset_source=1 \
        "bc.load_model_path=$init" \
        "bc.output=$output" \
        policy.hidden_size=128 \
        policy.num_layers=2 2>&1 | tee "$log"
done
