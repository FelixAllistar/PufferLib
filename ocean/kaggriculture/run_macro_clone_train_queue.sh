#!/usr/bin/env bash
set -euo pipefail

# Long-running Vast queue for the episode-safe macro clone refresh.  This
# script deliberately waits for any active native Kaggriculture training
# process and for the GPU to become idle; a fixed PID is unsafe because the
# user's training process may be restarted by its owner.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
data_root=${KAG_ELITE_DATA_ROOT:-/workspace/elite_replays}
python_bin=${KAG_ELITE_PYTHON:-/usr/bin/python3}

new_full="$data_root/clone_factory_macro2_train0831/datasets"
new_split="$data_root/clone_factory_macro2_train0831_episode_split"
old_full="$data_root/clone_factory_macro2_old0821/datasets"
old_split="$data_root/clone_factory_macro2_old0821_episode_split"

external_training_active() {
    # The owner may restart ./puffer, so match its command shape rather than
    # a stale PID.  Any matching process is treated as external and left
    # completely untouched.
    ps -eo comm=,args= | awk \
        '$1 == "puffer" && $3 == "train" && $4 == "kaggriculture" { found = 1 } END { exit !found }'
}

gpu_busy() {
    nvidia-smi --query-compute-apps=pid --format=csv,noheader 2>/dev/null \
        | grep -q '[0-9]'
}

wait_for_external_and_gpu() {
    while external_training_active || gpu_busy; do
        sleep 30
    done
}

split_one() {
    local full=$1 splitroot=$2 slug=$3
    local train="$splitroot/datasets/${slug}_cutoff-${4}_v1.32.7_macro2_episode_split.bc"
    local holdout="$splitroot/holdout/${slug}_holdout-latest.bc"
    local report="$splitroot/reports/${slug}_split.json"
    if [[ -s "$train" && -s "$holdout" && -s "$report" ]]; then
        echo "REUSE SPLIT $slug"
        return
    fi
    mkdir -p "$splitroot/datasets" "$splitroot/holdout" "$splitroot/reports"
    "$python_bin" "$repo_root/ocean/kaggriculture/split_macro_dataset.py" \
        "$full" --train-output "$train" --holdout-output "$holdout" \
        --report "$report"
}

required=(
    "$new_full/Crop_Dusta_cutoff-2026-08-31_v1.32.7_macro2.bc"
    "$new_full/Ryo_Hasegawa_cutoff-2026-08-31_v1.32.7_macro2.bc"
    "$new_full/peikopon_cutoff-2026-08-31_v1.32.7_macro2.bc"
    "$new_full/tetsuya_cutoff-2026-08-31_v1.32.7_macro2.bc"
    "$data_root/clone_factory_macro2_holdout0901/datasets/Crop_Dusta_cutoff-2026-09-01_v1.32.7_macro2.bc"
    "$data_root/clone_factory_macro2_holdout0901/datasets/tetsuya_cutoff-2026-09-01_v1.32.7_macro2.bc"
    "$old_full/Crop_Dusta_cutoff-2026-08-21_v1.32.7_macro2.bc"
    "$old_full/Ryo_Hasegawa_cutoff-2026-08-21_v1.32.7_macro2.bc"
    "$old_full/peikopon_cutoff-2026-08-21_v1.32.7_macro2.bc"
    "$old_full/tetsuya_cutoff-2026-08-21_v1.32.7_macro2.bc"
)
while :; do
    ready=1
    for path in "${required[@]}"; do
        [[ -s "$path" ]] || { ready=0; break; }
    done
    ((ready)) && break
    sleep 30
done

for spec in \
    "$new_full $new_split Crop_Dusta 2026-08-31" \
    "$new_full $new_split Ryo_Hasegawa 2026-08-31" \
    "$new_full $new_split peikopon 2026-08-31" \
    "$new_full $new_split tetsuya 2026-08-31" \
    "$old_full $old_split Crop_Dusta 2026-08-21" \
    "$old_full $old_split Ryo_Hasegawa 2026-08-21" \
    "$old_full $old_split peikopon 2026-08-21" \
    "$old_full $old_split tetsuya 2026-08-21"; do
    read -r full splitroot slug cutoff <<< "$spec"
    split_one "$full/${slug}_cutoff-${cutoff}_v1.32.7_macro2.bc" "$splitroot" "$slug" "$cutoff"
done

mkdir -p "$data_root"
log="$data_root/macro-train-2026-09-03.log"
{
    wait_for_external_and_gpu
    echo "TRAIN START $(date -u +%FT%TZ)"
    KAG_MACRO_CLONE_ROOT="$new_split" \
    KAG_MACRO_TRAIN_UNTIL=2026-08-31 \
    KAG_MACRO_ARTIFACT_SUFFIX=_episode_split \
    KAG_MACRO_EPOCHS=25 KAG_MACRO_MAX_EPISODES=0 \
        "$repo_root/ocean/kaggriculture/build_macro_clone_factory.sh" \
        "Crop Dusta" "Ryo Hasegawa" peikopon tetsuya
    echo "OLD TRAIN START $(date -u +%FT%TZ)"
    KAG_MACRO_CLONE_ROOT="$old_split" \
    KAG_MACRO_TRAIN_UNTIL=2026-08-21 \
    KAG_MACRO_ARTIFACT_SUFFIX=_episode_split \
    KAG_MACRO_EPOCHS=25 KAG_MACRO_MAX_EPISODES=0 \
        "$repo_root/ocean/kaggriculture/build_macro_clone_factory.sh" \
        "Crop Dusta" "Ryo Hasegawa" peikopon tetsuya
    echo "TRAIN COMPLETE $(date -u +%FT%TZ)"
} >"$log" 2>&1
