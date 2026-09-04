#!/usr/bin/env bash
set -euo pipefail

# Train one deliberately short, evaluable pass over the full-season elite
# replay dataset. Continue with KAG_ELITE_BC_INIT only after the prior model has
# passed simulator evaluation; this avoids losing a useful clone to overfit.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data=${KAG_ELITE_BC_DATA:-/workspace/elite_replays/kaggriculture_elite_1.32.7.bc}
epochs=${1:-5}
hidden=${KAG_ELITE_BC_HIDDEN:-1024}
layers=${KAG_ELITE_BC_LAYERS:-2}
output=${KAG_ELITE_BC_OUTPUT:-saved/kaggriculture_elite_bc_1280_${hidden}x${layers}_e${epochs}.bin}
init=${KAG_ELITE_BC_INIT:-None}
batch=${KAG_ELITE_BC_BATCH:-32}
validation_games=${KAG_ELITE_BC_VALIDATION_GAMES:-400}
opening_steps=${KAG_ELITE_BC_OPENING_STEPS:-1}
opening_weight=${KAG_ELITE_BC_OPENING_WEIGHT:-1}
root_weight=${KAG_ELITE_BC_ROOT_WEIGHT:-$opening_weight}
seed=${KAG_ELITE_BC_SEED:-42}
argmax_margin=${KAG_ELITE_BC_ARGMAX_MARGIN:-0}
opening_argmax_coef=${KAG_ELITE_BC_OPENING_ARGMAX_COEF:-0}
learning_rate=${KAG_ELITE_BC_LR:-0.00005}
report_interval=${KAG_ELITE_BC_REPORT_INTERVAL:-1}
detailed_stats=${KAG_ELITE_BC_DETAILED_STATS:-0}
macro_class_balance=${KAG_ELITE_BC_MACRO_CLASS_BALANCE:-0}
macro_class_weight_cap=${KAG_ELITE_BC_MACRO_CLASS_WEIGHT_CAP:-8}
anchor_l2=${KAG_ELITE_BC_ANCHOR_L2:-0}

if [[ ! -s "$data" ]]; then
    echo "elite BC dataset not found: $data" >&2
    exit 1
fi
mkdir -p "$(dirname "$output")"
make -C "$repo_root/ocean/kaggriculture" build/kag_bc

cd "$repo_root"
exec ./ocean/kaggriculture/build/kag_bc \
    bc.mode=train \
    bc.data="$data" \
    bc.output="$output" \
    bc.load_model_path="$init" \
    bc.epochs="$epochs" \
    bc.learning_rate="$learning_rate" \
    bc.batch="$batch" \
    bc.seed="$seed" \
    bc.validation_games="$validation_games" \
    bc.report_interval="$report_interval" \
    bc.opening_steps="$opening_steps" \
    bc.opening_weight="$opening_weight" \
    bc.root_weight="$root_weight" \
    bc.argmax_margin="$argmax_margin" \
    bc.opening_argmax_coef="$opening_argmax_coef" \
    bc.detailed_stats="$detailed_stats" \
    bc.macro_class_balance="$macro_class_balance" \
    bc.macro_class_weight_cap="$macro_class_weight_cap" \
    bc.anchor_l2="$anchor_l2" \
    bc.zero_reset_source=0 \
    policy.hidden_size="$hidden" \
    policy.num_layers="$layers"
