#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

phase1_steps="${1:-5000000}"
phase2_steps="${2:-30000000}"
phase3_steps="${3:-50000000}"
phase3_long_steps="${4:-75000000}"
phase4_steps="${5:-75000000}"
phase5_steps="${6:-100000000}"
hidden_size="${KAG_HIDDEN_SIZE:-32}"
num_layers="${KAG_NUM_LAYERS:-2}"
stop_after="${KAG_STOP_AFTER:-}"
extra_overrides=()
if [[ -n ${KAG_OVERRIDES:-} ]]; then
    read -r -a extra_overrides <<< "$KAG_OVERRIDES"
fi
if [[ -n ${KAG_ARCHIVE:-} ]]; then
    archive=$KAG_ARCHIVE
elif [[ $hidden_size == 32 && $num_layers == 2 ]]; then
    archive=saved/kaggriculture_v2
else
    archive="saved/kaggriculture_v2_h${hidden_size}_l${num_layers}"
fi
[[ $hidden_size =~ ^[0-9]+$ && $hidden_size -gt 0 ]] || exit 2
[[ $num_layers =~ ^[0-9]+$ && $num_layers -gt 0 ]] || exit 2
case "$stop_after" in
    ''|phase1|phase2|phase3|phase3_long|phase4|phase5) ;;
    *) echo "KAG_STOP_AFTER must be phase1, phase2, phase3, phase3_long, phase4, or phase5" >&2; exit 2 ;;
esac
arch_overrides=(
    "policy.hidden_size=$hidden_size"
    "policy.num_layers=$num_layers"
    "vec.frozen_bank_hidden_size=$hidden_size"
    "vec.frozen_bank_num_layers=$num_layers"
)
mkdir -p "$archive"
cc -O3 -std=c17 -Wall -Wextra -Werror \
    ocean/kaggriculture/checkpoint_gate.c -o ocean/kaggriculture/build/checkpoint_gate

latest_checkpoint() {
    find checkpoints/kaggriculture -type f -name '*.bin' -printf '%T@ %p\n' \
        | sort -nr | sed -n '1p' | cut -d' ' -f2-
}

stop_if_requested() {
    if [[ $stop_after == "$1" ]]; then
        echo "Kaggriculture v2 stopped after $1: $2"
        exit 0
    fi
}

echo "Kaggriculture v2 phase 1: five-day wheat maintenance (${phase1_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    selfplay.enabled=0 \
    env.curriculum_stage=1 \
    env.episode_steps=120 \
    env.weed_spawn_chance=0 \
    train.ent_coef=0.01 \
    train.anneal_ent_coef=0 \
    train.total_timesteps="$phase1_steps" \
    "${extra_overrides[@]}"
phase1="$(latest_checkpoint)"
cp "$phase1" "$archive/phase1_wheat.bin"
stop_if_requested phase1 "$archive/phase1_wheat.bin"

echo "Kaggriculture v2 phase 2: fixed-seed crop scheduling (${phase2_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase1_wheat.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase1_wheat.bin" \
    env.curriculum_stage=2 \
    env.episode_steps=360 \
    env.bot_opponent_fraction=0.25 \
    train.ent_coef=0.006 \
    train.total_timesteps="$phase2_steps" \
    "${extra_overrides[@]}"
phase2="$(latest_checkpoint)"
cp "$phase2" "$archive/phase2_scheduling.bin"
stop_if_requested phase2 "$archive/phase2_scheduling.bin"

echo "Kaggriculture v2 phase 3: one-quadrant crop economy (${phase3_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase2_scheduling.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase1_wheat.bin,$archive/phase2_scheduling.bin" \
    env.curriculum_stage=3 \
    env.episode_steps=360 \
    env.bot_opponent_fraction=0.5 \
    train.ent_coef=0.004 \
    train.total_timesteps="$phase3_steps" \
    "${extra_overrides[@]}"
phase3="$(latest_checkpoint)"
cp "$phase3" "$archive/phase3_economy.bin"
stop_if_requested phase3 "$archive/phase3_economy.bin"

echo "Kaggriculture v2 phase 3b: full-season crop economy (${phase3_long_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase3_economy.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase2_scheduling.bin,$archive/phase3_economy.bin" \
    env.curriculum_stage=4 \
    env.episode_steps=720 \
    env.bot_opponent_fraction=0.5 \
    train.ent_coef=0.004 \
    train.total_timesteps="$phase3_long_steps" \
    "${extra_overrides[@]}"
phase3_long="$(latest_checkpoint)"
cp "$phase3_long" "$archive/phase3_long_season.bin"
stop_if_requested phase3_long "$archive/phase3_long_season.bin"

ocean/kaggriculture/build/checkpoint_gate \
    "$archive/phase3_long_season.bin" "$archive/phase3_land_seed.bin" land \
    "$hidden_size" "$num_layers"

echo "Kaggriculture v2 phase 4: gated crop land purchases (${phase4_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase3_land_seed.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase3_economy.bin,$archive/phase3_long_season.bin" \
    env.curriculum_stage=5 \
    env.episode_steps=720 \
    env.bot_opponent_fraction=0.75 \
    train.ent_coef=0.003 \
    train.total_timesteps="$phase4_steps" \
    "${extra_overrides[@]}"
phase4="$(latest_checkpoint)"
cp "$phase4" "$archive/phase4_crop_land.bin"
stop_if_requested phase4 "$archive/phase4_crop_land.bin"

ocean/kaggriculture/build/checkpoint_gate \
    "$archive/phase4_crop_land.bin" "$archive/phase4_full_seed.bin" full \
    "$hidden_size" "$num_layers"

echo "Kaggriculture v2 phase 5: gated unrestricted game (${phase5_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase4_full_seed.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase3_long_season.bin,$archive/phase4_crop_land.bin" \
    env.curriculum_stage=6 \
    env.episode_steps=720 \
    env.bot_opponent_fraction=0.75 \
    train.ent_coef=0.0003 \
    train.total_timesteps="$phase5_steps" \
    "${extra_overrides[@]}"
phase5="$(latest_checkpoint)"
cp "$phase5" "$archive/phase5_full.bin"
stop_if_requested phase5 "$archive/phase5_full.bin"

echo "Kaggriculture v2 complete: $archive/phase5_full.bin"
