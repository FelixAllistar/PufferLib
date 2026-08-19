#!/bin/bash
# Flip the Kaggriculture sweep objective between staged search spaces.
# All [sweep.*] ranges already exist in config/kaggriculture.ini; this only
# rewrites sweep.sweep_only so each stage stays low-dimensional.
#
# Usage:
#   set_sweep_mode.sh train      # lr, entropy, EMAg, win, potential (current)
#   set_sweep_mode.sh assets     # asset weights on top of the train stage
#   set_sweep_mode.sh penalties  # neglect/liquidation/margin on top of assets
#   set_sweep_mode.sh all        # everything (confounded; not recommended)
set -euo pipefail

cd "$(dirname "$0")/../.."
kag_mode=${1:-train}
kag_cfg=config/kaggriculture.ini

kag_train="train.learning_rate,train.ent_coef,train.emag_kl_coef,env.reward_win,env.reward_potential_scale"
kag_assets="env.reward_seed_value,env.reward_product_value,env.reward_crop_value,env.reward_animal_value,env.reward_land_value,env.reward_neglect_discount"
kag_penalties="env.reward_liquidation_days,env.reward_margin_scale"

case "$kag_mode" in
    train)     kag_keys="$kag_train" ;;
    assets)    kag_keys="$kag_train,$kag_assets" ;;
    penalties) kag_keys="$kag_train,$kag_assets,$kag_penalties" ;;
    all)       kag_keys="$kag_train,$kag_assets,$kag_penalties,env.reward_productive_action,env.reward_inactivity,env.reward_neglect_death" ;;
    *)         echo "unknown mode: $kag_mode" >&2; echo "use train|assets|penalties|all" >&2; exit 2 ;;
esac

sed -i "s|^sweep_only = .*|sweep_only = $kag_keys|" "$kag_cfg"
printf 'sweep mode: %s\n%s\n' "$kag_mode" \
    "$(grep '^sweep_only' "$kag_cfg")"
