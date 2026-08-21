#!/bin/bash
# Behavioral-clone full 720-step (719-decision) episodes into full-game bases.
# Unlike clone_bots.sh (96-step EMAg anchors), these models see the whole
# season and are intended as `load_model_path` warm starts, not opening-only
# KL magnets. A 720-turn episode exposes 719 decision observations; the
# generator infers that exact length from the first finished game.
#
# Tunables:
#   KAG_BC_GAMES   games per dataset (default 150)
#   KAG_BC_EPOCHS  training epochs (default 300)
#   KAG_BC_BATCH   games per training batch (default 16)
#   KAG_BC_LR      training learning rate (default 0.001)
#   KAG_BC_OUT_DIR output directory (default saved/kaggriculture_bc_v2)
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_bc=./ocean/kaggriculture/build/kag_bc
kag_out=${KAG_BC_OUT_DIR:-saved/kaggriculture_bc_v2}
kag_games=${KAG_BC_GAMES:-150}
kag_epochs=${KAG_BC_EPOCHS:-300}
kag_batch=${KAG_BC_BATCH:-16}
kag_lr=${KAG_BC_LR:-0.001}

mkdir -p "$kag_out"

declare -A kag_profile=(
    [rules]=0 [top]=1 [structured]=2 [pulse]=3 [frontier]=4
    [triad]=5 [thunder]=6 [lugovoy]=7 [thunder25]=8 [v20]=9
    [moon]=10 [hamburger]=11
)

kag_usage() {
    printf 'usage: %s NAME [NAME ...]\n' "$0" >&2
    printf 'available: rules top structured pulse frontier triad thunder lugovoy thunder25 v20 moon hamburger\n' >&2
}

if (($# == 0)); then
    kag_usage
    exit 2
fi

for kag_name in "$@"; do
    kag_profile_id=${kag_profile[$kag_name]:-}
    if [[ -z $kag_profile_id ]]; then
        printf 'unknown bot: %s\n' "$kag_name" >&2
        kag_usage
        exit 2
    fi

    kag_data="$kag_out/${kag_name}_full_data.bin"
    kag_model="$kag_out/${kag_name}_full_clone.bin"

    printf '== full-game clone %s (profile %s) ==\n' "$kag_name" "$kag_profile_id"
    "$kag_bc" bc.mode=gen bc.games="$kag_games" bc.steps=720 \
        bc.bot="$kag_profile_id" bc.opponent=-1 bc.seat=-1 \
        bc.rollout_noise=0.05 bc.seed=2903 bc.data="$kag_data"
    "$kag_bc" bc.mode=train bc.data="$kag_data" \
        bc.epochs="$kag_epochs" bc.batch="$kag_batch" \
        bc.learning_rate="$kag_lr" \
        bc.opening_steps=26 bc.opening_weight=1 bc.validation_games=30 \
        bc.zero_reset_source=0 bc.output="$kag_model" \
        policy.hidden_size=128 policy.num_layers=2
    printf 'full-game clone: %s\n' "$kag_model"
done
