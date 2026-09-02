#!/usr/bin/env bash
set -euo pipefail

# Evaluate matching fresh/old episode-split clones at one hidden width.  The
# first pass writes COMPLETE; a 256-wide invocation may require that marker
# so the fixed-opponent GPU jobs never overlap the 128-wide pass.

if (($# != 1)) || ! [[ $1 =~ ^(128|256)$ ]]; then
    echo "usage: $0 128|256" >&2
    exit 2
fi
width=$1
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data_root=${KAG_ELITE_DATA_ROOT:-/workspace/elite_replays}
python_bin=${KAG_ELITE_PYTHON:-/usr/bin/python3}
new_models="$data_root/clone_factory_macro2_train0831_episode_split/models"
old_models="$data_root/clone_factory_macro2_old0821_episode_split/models"
holdout_root="$data_root/clone_factory_macro2_holdout0901/datasets"
new_holdout_root="$data_root/clone_factory_macro2_train0831_episode_split/holdout"
output_root="$data_root/evals_2026-09-03"

external_training_active() {
    ps -eo comm=,args= | awk \
        '$1 == "puffer" && $3 == "train" && $4 == "kaggriculture" { found = 1 } END { exit !found }'
}

gpu_busy() {
    nvidia-smi --query-compute-apps=pid --format=csv,noheader 2>/dev/null \
        | grep -q '[0-9]'
}

if [[ "$width" == 256 ]]; then
    while [[ ! -s "$output_root/COMPLETE" ]]; do sleep 30; done
fi

required=(
    "$new_models/Crop_Dusta_cutoff-2026-08-31_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    "$new_models/Ryo_Hasegawa_cutoff-2026-08-31_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    "$new_models/peikopon_cutoff-2026-08-31_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    "$new_models/tetsuya_cutoff-2026-08-31_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    "$old_models/Crop_Dusta_cutoff-2026-08-21_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    "$old_models/Ryo_Hasegawa_cutoff-2026-08-21_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    "$old_models/peikopon_cutoff-2026-08-21_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    "$old_models/tetsuya_cutoff-2026-08-21_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
)
while :; do
    ready=1
    for path in "${required[@]}"; do
        [[ -s "$path" ]] || { ready=0; break; }
    done
    ((ready)) && break
    sleep 30
done
while external_training_active || gpu_busy; do sleep 30; done

mkdir -p "$output_root"
for spec in \
    "Crop_Dusta 2026-09-01" "Ryo_Hasegawa split" \
    "peikopon split" "tetsuya 2026-09-01"; do
    read -r slug holdtag <<< "$spec"
    new="$new_models/${slug}_cutoff-2026-08-31_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    old="$old_models/${slug}_cutoff-2026-08-21_v1.32.7_macro2_episode_split_h${width}x2_s2903_e25.bin"
    if [[ "$holdtag" == 2026-09-01 ]]; then
        hold="$holdout_root/${slug}_cutoff-2026-09-01_v1.32.7_macro2.bc"
    else
        hold="$new_holdout_root/${slug}_holdout-latest.bc"
    fi
    "$python_bin" "$repo_root/ocean/kaggriculture/evaluate_macro_clone.py" \
        "$hold" "$new" --holdout-fraction 1.0 \
        --output "$output_root/${slug}_new${width}_on_holdout.json" \
        >"$output_root/${slug}_new${width}_on_holdout.log" 2>&1
    "$python_bin" "$repo_root/ocean/kaggriculture/evaluate_macro_clone.py" \
        "$hold" "$old" --holdout-fraction 1.0 \
        --output "$output_root/${slug}_old${width}_on_holdout.json" \
        >"$output_root/${slug}_old${width}_on_holdout.log" 2>&1
    "$repo_root/ocean/kaggriculture/eval_population.sh" \
        --games 20 --jobs 1 --gpu-agents 16 --fixed pass,rules,top \
        --hidden-size "$width" --num-layers 2 \
        --output "$output_root/${slug}_rollout_det_${width}" "$new" "$old" \
        >"$output_root/${slug}_rollout_det_${width}.log" 2>&1
    "$repo_root/ocean/kaggriculture/eval_population.sh" \
        --games 20 --jobs 1 --gpu-agents 16 --fixed pass,rules,top \
        --hidden-size "$width" --num-layers 2 --stochastic \
        --output "$output_root/${slug}_rollout_stoch_${width}" "$new" "$old" \
        >"$output_root/${slug}_rollout_stoch_${width}.log" 2>&1
done

if [[ "$width" == 128 ]]; then
    marker="$output_root/COMPLETE"
else
    marker="$output_root/COMPLETE_256"
fi
echo "EVAL $width COMPLETE $(date -u +%FT%TZ)" >"$marker"
