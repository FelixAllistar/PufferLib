#!/usr/bin/env bash
# 512x3 staged task curriculum, modeled on the original train_v2.sh. The task
# itself grows (5-day maintenance -> full season -> land), while opponents
# build on the same-architecture phase checkpoints. The old checkpoint_gate.c
# was tied to the retired 1156-byte observation ABI and is intentionally
# omitted; promotion is instead done after the run with eval_sweeps/PSRO.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

phase1_steps=${1:-5000000}
phase2_steps=${2:-30000000}
phase3_steps=${3:-50000000}
phase3_long_steps=${4:-75000000}
phase4_steps=${5:-75000000}
phase5_steps=${6:-100000000}
hidden=512
layers=3
archive=saved/kaggriculture_v2_h512_l3
mkdir -p "$archive"

latest_checkpoint() {
    find checkpoints/kaggriculture -type f -name '*.bin' -printf '%T@ %p\n' \
        | sort -nr | sed -n '1p' | cut -d' ' -f2-
}

arch_overrides=(
    "policy.hidden_size=$hidden"
    "policy.num_layers=$layers"
    "vec.frozen_bank_hidden_size=$hidden"
    "vec.frozen_bank_num_layers=$layers"
)

echo "V2 512x3 phase 1: five-day wheat maintenance (${phase1_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    selfplay.enabled=0 \
    env.episode_steps=120 \
    env.weed_spawn_chance=0 \
    train.ent_coef=0.01 \
    train.total_timesteps="$phase1_steps"
phase1="$(latest_checkpoint)"
cp "$phase1" "$archive/phase1_wheat.bin"

echo "V2 512x3 phase 2: fixed-seed crop scheduling (${phase2_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase1_wheat.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase1_wheat.bin" \
    env.episode_steps=360 \
    env.bot_opponent_fraction=0.25 \
    train.ent_coef=0.006 \
    train.total_timesteps="$phase2_steps"
phase2="$(latest_checkpoint)"
cp "$phase2" "$archive/phase2_scheduling.bin"

echo "V2 512x3 phase 3: one-quadrant crop economy (${phase3_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase2_scheduling.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase1_wheat.bin,$archive/phase2_scheduling.bin" \
    env.episode_steps=360 \
    env.bot_opponent_fraction=0.5 \
    train.ent_coef=0.004 \
    train.total_timesteps="$phase3_steps"
phase3="$(latest_checkpoint)"
cp "$phase3" "$archive/phase3_economy.bin"

echo "V2 512x3 phase 3b: full-season crop economy (${phase3_long_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase3_economy.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase2_scheduling.bin,$archive/phase3_economy.bin" \
    env.episode_steps=720 \
    env.bot_opponent_fraction=0.5 \
    train.ent_coef=0.004 \
    train.total_timesteps="$phase3_long_steps"
phase3_long="$(latest_checkpoint)"
cp "$phase3_long" "$archive/phase3_long_season.bin"

echo "V2 512x3 phase 4: gated crop land purchases (${phase4_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase3_long_season.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase3_economy.bin,$archive/phase3_long_season.bin" \
    env.episode_steps=720 \
    env.bot_opponent_fraction=0.75 \
    train.ent_coef=0.003 \
    train.total_timesteps="$phase4_steps"
phase4="$(latest_checkpoint)"
cp "$phase4" "$archive/phase4_crop_land.bin"

echo "V2 512x3 phase 5: gated unrestricted game (${phase5_steps} steps)"
./puffer train kaggriculture \
    "${arch_overrides[@]}" \
    base.load_model_path="$archive/phase4_crop_land.bin" \
    selfplay.enabled=1 \
    selfplay.opponent_pool="$archive/phase3_long_season.bin,$archive/phase4_crop_land.bin" \
    env.episode_steps=720 \
    env.bot_opponent_fraction=0.75 \
    train.ent_coef=0.0003 \
    train.total_timesteps="$phase5_steps"
phase5="$(latest_checkpoint)"
cp "$phase5" "$archive/phase5_full.bin"

echo "V2 512x3 complete: $archive/phase5_full.bin"
