#!/usr/bin/env bash
set -euo pipefail

# Teach an established root crop policy additional animal/economic behavior
# without anchoring it to the cow-only S6/S8 attractor. Reset probability is
# tapered after every root-only checkpoint promotion.
ROOT="${KAG_ROOT:-/workspace/PufferLib}"
FULL_BANK="${KAG_FULL_BANK:-/workspace/elite_replays/state_bank/full_1365_each.kgb}"
EARLY_BANK="${KAG_EARLY_BANK:-/workspace/elite_replays/state_bank/full_early_t0_240.kgb}"
EARLY_MID_BANK="${KAG_EARLY_MID_BANK:-/workspace/elite_replays/state_bank/full_early_mid_t0_480.kgb}"
SOURCE="${KAG_SOURCE:-saved/kaggriculture_league_512x2_elite_v5/crop_499m.bin}"
PYTHON="${KAG_PYTHON:-/venv/main/bin/python}"
GAMES="${KAG_PROMOTION_GAMES:-20}"

cd "$ROOT"

if [[ ! -s "$EARLY_BANK" ]]; then
    "$PYTHON" ocean/kaggriculture/slice_replay_state_bank.py \
        --bank "$FULL_BANK" --output "$EARLY_BANK" \
        --stage full --min-turn 0 --max-turn 240
fi
if [[ ! -s "$EARLY_MID_BANK" ]]; then
    "$PYTHON" ocean/kaggriculture/slice_replay_state_bank.py \
        --bank "$FULL_BANK" --output "$EARLY_MID_BANK" \
        --stage full --min-turn 0 --max-turn 480
fi

checkpoint_for_policy() {
    local prefix="$1"
    local policy
    policy="$(awk -F '\t' 'NR == 2 {print $2}' "${prefix}_ranking.tsv")"
    awk -F '\t' -v policy="$policy" 'NR > 1 && $2 == policy {print $3; exit}' \
        "${prefix}_manifest.tsv"
}

promote_run() {
    local run="$1"
    local prefix="logs/kaggriculture/${run}_root_det"
    KAG_FIXED_SEED_A=20000 KAG_FIXED_SEED_B=21000 \
        ./ocean/kaggriculture/eval_population.sh \
        --games "$GAMES" --gpu-agents 64 --sample-run 4 \
        --fixed pass,rules --output "$prefix" \
        "checkpoints/kaggriculture/$run" >&2
    checkpoint_for_policy "$prefix"
}

train_stage() {
    local run="$1"
    local load="$2"
    local bank="$3"
    local reset_prob="$4"
    local steps="$5"
    local replay_ratio="$6"
    local entropy="$7"
    if [[ -d "checkpoints/kaggriculture/$run" ]]; then
        echo "Refusing to overwrite existing run: $run" >&2
        exit 1
    fi
    echo "START $run reset=$reset_prob steps=$steps replay=$replay_ratio source=$load"
    ./puffer train kaggriculture \
        base.run_id="$run" \
        base.load_model_path="$load" \
        base.cudagraphs=0 \
        selfplay.magnet_path=None \
        env.reset_state_bank="$bank" \
        env.reset_state_prob="$reset_prob" \
        vec.total_agents=4096 \
        train.total_timesteps="$steps" \
        train.horizon=256 \
        train.replay_ratio="$replay_ratio" \
        train.learning_rate=0.00015 \
        train.ent_coef="$entropy" \
        train.emag_kl_coef=0 \
        train.emag_tau=0
}

stage1=cropmix_s9_early80_512x2_v1
train_stage "$stage1" "$SOURCE" "$EARLY_BANK" 0.80 200000000 2 0.0012
SOURCE="$(promote_run "$stage1")"
echo "PROMOTE $stage1 source=$SOURCE"

stage2=cropmix_s10_earlymid40_512x2_v1
train_stage "$stage2" "$SOURCE" "$EARLY_MID_BANK" 0.40 200000000 2 0.0010
SOURCE="$(promote_run "$stage2")"
echo "PROMOTE $stage2 source=$SOURCE"

stage3=cropmix_s11_full10_512x2_v1
train_stage "$stage3" "$SOURCE" "$FULL_BANK" 0.10 200000000 1 0.0007
SOURCE="$(promote_run "$stage3")"
echo "PROMOTE $stage3 source=$SOURCE"

stage4=cropmix_s12_root_512x2_v1
train_stage "$stage4" "$SOURCE" "$FULL_BANK" 0 300000000 1 0.0005
SOURCE="$(promote_run "$stage4")"
echo "CROP-TO-MIXED CURRICULUM COMPLETE best=$SOURCE"
