#!/bin/bash
# Behavioral-clone one or more native Kaggriculture bots into frozen EMAg
# anchors. The "top" profile uses the two-phase opening+recovery pipeline;
# every other profile is a single-phase recurrent clone.
#
# Tunables:
#   KAG_BC_GAMES   games per dataset (default 200)
#   KAG_BC_STEPS   decision steps per game (default 96, matching emag_cutoff)
#   KAG_BC_EPOCHS  training epochs (default 400)
#   KAG_BC_BATCH   games per training batch (default 32)
#   KAG_BC_LR      training learning rate (default 0.0002)
#   KAG_BC_OUT_DIR output directory (default saved/kaggriculture_bc_v2)
#   KAG_BC_HIDDEN  policy width (default 128)
#   KAG_BC_LAYERS  policy depth (default 2)
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_bc=./ocean/kaggriculture/build/kag_bc
kag_out=${KAG_BC_OUT_DIR:-saved/kaggriculture_bc_v2}
kag_games=${KAG_BC_GAMES:-200}
kag_steps=${KAG_BC_STEPS:-96}
kag_epochs=${KAG_BC_EPOCHS:-1000}
kag_batch=${KAG_BC_BATCH:-32}
kag_lr=${KAG_BC_LR:-0.001}
kag_hidden=${KAG_BC_HIDDEN:-128}
kag_layers=${KAG_BC_LAYERS:-2}

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

    kag_data="$kag_out/${kag_name}_data.bin"
    if [[ $kag_hidden == 128 && $kag_layers == 2 ]]; then
        kag_model="$kag_out/${kag_name}_clone.bin"
    else
        kag_model="$kag_out/${kag_name}_clone_h${kag_hidden}_l${kag_layers}.bin"
    fi

    printf '== clone %s (profile %s) ==\n' "$kag_name" "$kag_profile_id"
    "$kag_bc" bc.mode=gen bc.games="$kag_games" bc.steps="$kag_steps" \
        bc.bot="$kag_profile_id" bc.opponent=-1 bc.seat=-1 \
        bc.rollout_noise=0.05 bc.seed=2903 bc.data="$kag_data"
    "$kag_bc" bc.mode=train bc.data="$kag_data" \
        bc.epochs="$kag_epochs" bc.batch="$kag_batch" \
        bc.learning_rate="$kag_lr" \
        bc.load_model_path=None \
        bc.opening_steps=26 bc.opening_weight=1 bc.validation_games=40 \
        bc.zero_reset_source=0 bc.output="$kag_model" \
        "policy.hidden_size=$kag_hidden" "policy.num_layers=$kag_layers"
    printf 'clone: %s\n' "$kag_model"
done
