#!/usr/bin/env bash
# Controller convenience wrapper. INI rewards/load path remain authoritative.
set -euo pipefail
dense_preset=0
if [[ ${1:-} == --dense-preset ]]; then
    dense_preset=1
    shift
fi
if (( $# < 2 )); then
    echo "Usage: bash ocean/kaggriculture/dense_experiment.sh [--dense-preset] MODE EXECUTOR [section.key=value ...]" >&2
    echo "Default: use config rewards and load_model_path. --dense-preset explicitly overrides rewards. For fresh weights add base.load_model_path=None." >&2
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
    "base.run_id=$dense_id" \
    "env.macro_mode=$dense_mode" "env.macro_executor_version=$dense_executor" \
    env.frozen_macro_mode=-1 env.frozen_macro_executor_version=-1 \
    env.frozen_macro_decision_interval=-1 env.frozen_macro_score_features=-1)
if (( dense_preset )); then
    dense_command+=( \
    env.reward_money_scale=1 env.reward_money_timing=1 \
    env.reward_quality_scale=0 env.reward_quality_timing=1 env.reward_quality_idle_cost=0.25 \
    env.reward_growth_land=1 env.reward_growth_crop=0.05 env.reward_growth_animal=0.25 \
    env.reward_alive_daily=0.05 env.reward_target_plots=3 env.reward_target_animals=15 env.reward_target_crops=-1 \
    env.reward_pbrs_scale=0 env.pbrs_cash_weight=1 env.pbrs_stock_weight=1 \
    env.pbrs_crop_weight=0.25 env.pbrs_animal_weight=0.25 \
    env.reward_potential_scale=0 env.reward_cash_scale=0 env.reward_progress_scale=0 \
    env.reward_progress_terminal_money_scale=0 env.reward_progress_win_scale=0 \
    env.reward_progress_maintenance_scale=0 env.reward_expansion_scale=0 env.reward_phase_scale=0)
fi
dense_command+=("$@")
if [[ ${KAG_DRY_RUN:-0} == 1 ]]; then
    printf '%q ' "${dense_command[@]}"
    printf '\n'
else
    exec "${dense_command[@]}"
fi
