#!/usr/bin/env bash
set -euo pipefail

# Train one deliberately short, evaluable pass over the full-season elite
# replay dataset. Continue with KAG_ELITE_BC_INIT only after the prior model has
# passed simulator evaluation; this avoids losing a useful clone to overfit.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data=${KAG_ELITE_BC_DATA:-/workspace/elite_replays/kaggriculture_elite_1.32.7.bc}
epochs=${1:-5}
output=${KAG_ELITE_BC_OUTPUT:-saved/kaggriculture_elite_bc_1280_1024x2_e${epochs}.bin}
init=${KAG_ELITE_BC_INIT:-None}
batch=${KAG_ELITE_BC_BATCH:-32}
validation_games=${KAG_ELITE_BC_VALIDATION_GAMES:-400}

if [[ ! -s "$data" ]]; then
    echo "elite BC dataset not found: $data" >&2
    exit 1
fi
mkdir -p "$(dirname "$repo_root/$output")"
make -C "$repo_root/ocean/kaggriculture" build/kag_bc

cd "$repo_root"
exec ./ocean/kaggriculture/build/kag_bc \
    bc.mode=train \
    bc.data="$data" \
    bc.output="$output" \
    bc.load_model_path="$init" \
    bc.epochs="$epochs" \
    bc.learning_rate=0.00005 \
    bc.batch="$batch" \
    bc.validation_games="$validation_games" \
    bc.report_interval=1 \
    bc.opening_steps=1 \
    bc.opening_weight=1 \
    bc.anchor_l2=0 \
    bc.zero_reset_source=0 \
    policy.hidden_size=1024 \
    policy.num_layers=2
