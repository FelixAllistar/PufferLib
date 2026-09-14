#!/usr/bin/env bash
# Fresh policy + dense reward preset. Final section.key=value arguments win.
set -euo pipefail
if (( $# < 2 )); then
    echo "Usage: bash ocean/kaggriculture/dense_experiment.sh MODE EXECUTOR [section.key=value ...]" >&2
    exit 2
fi
dense_mode=$1
dense_executor=$2
shift 2
case "$dense_mode:$dense_executor" in
    0:0|1:0|1:1|2:0|2:1|3:0) ;;
    *) echo "Modes 0/3 have their own decoders; executor 0/1 applies to modes 1/2." >&2; exit 2 ;;
esac
dense_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$dense_root"
dense_id="dense_m${dense_mode}_e${dense_executor}_$(date -u +%Y%m%d_%H%M%S)_$$"
dense_command=(./puffer train kaggriculture \
    "base.run_id=$dense_id" base.load_model_path=None \
    env.observation_version=3 env.frozen_observation_version=3 \
    "env.macro_mode=$dense_mode" "env.macro_executor_version=$dense_executor" \
    env.frozen_macro_mode=-1 env.frozen_macro_executor_version=-1 \
    env.macro_decision_interval=1 env.frozen_macro_decision_interval=-1 \
    env.macro_score_features=0 env.frozen_macro_score_features=-1 \
    env.policy_max_hands=16 env.policy_market_slots=10 env.curriculum_enabled=0 \
    env.reward_money_scale=1 env.reward_money_timing=1 \
    env.reward_quality_scale=0 env.reward_quality_timing=1 env.reward_quality_idle_cost=0.25 \
    env.reward_growth_land=1 env.reward_growth_crop=0.05 env.reward_growth_animal=0.25 \
    env.reward_alive_daily=0.05 env.reward_target_plots=3 env.reward_target_animals=15 env.reward_target_crops=-1 \
    env.reward_pbrs_scale=0 env.pbrs_cash_weight=1 env.pbrs_stock_weight=1 \
    env.pbrs_crop_weight=0.25 env.pbrs_animal_weight=0.25 \
    env.reward_potential_scale=0 env.reward_cash_scale=0 env.reward_progress_scale=0 \
    env.reward_progress_terminal_money_scale=0 env.reward_progress_win_scale=0 \
    env.reward_progress_maintenance_scale=0 env.reward_expansion_scale=0 env.reward_phase_scale=0 \
    "$@")
if [[ ${KAG_DRY_RUN:-0} == 1 ]]; then
    printf '%q ' "${dense_command[@]}"
    printf '\n'
else
    exec "${dense_command[@]}"
fi
