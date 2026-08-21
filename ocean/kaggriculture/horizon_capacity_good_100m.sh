#!/bin/bash
# Continue the GDP-positive phase-2 candidates for 100M additional steps.
# Only the existing candidate axes, load checkpoint, run id, and fixed trial
# length are supplied; rewards/self-play/bots/model settings remain in the
# current remote config.
set -euo pipefail

cd "$(dirname "$0")/../.."
kag_steps=${STEPS:-100000000}
kag_out=logs/kaggriculture/horizon_capacity_good_100m.tsv
mkdir -p logs/kaggriculture
[[ -f $kag_out ]] || printf 'run\tparent\thorizon\tagents\tminibatch\tsps\tpotential\tmoney\tgdp\n' > "$kag_out"

kag_specs=(
    "matrix_sp20m_h64_a16384_m2048_stage2 64 16384 2048"
    "matrix_sp20m_h64_a16384_m4096_stage2 64 16384 4096"
    "matrix_sp20m_h128_a8192_m4096_stage2 128 8192 4096"
)

for kag_spec in "${kag_specs[@]}"; do
    read -r kag_parent kag_h kag_a kag_mb <<< "$kag_spec"
    kag_ckpt=$(find "checkpoints/kaggriculture/$kag_parent" -maxdepth 1 -type f -name '*.bin' \
        -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -1 | cut -d' ' -f2- || true)
    if [[ -z $kag_ckpt || ! -f $kag_ckpt ]]; then
        printf 'SKIP %s: no checkpoint found\n' "$kag_parent"
        continue
    fi

    kag_run="${kag_parent}_stage3_100m"
    kag_log="logs/kaggriculture/${kag_run}.log"
    if grep -qF "$kag_run" "$kag_out" 2>/dev/null; then
        printf 'SKIP %s: already recorded\n' "$kag_run"
        continue
    fi
    printf 'RUN %s horizon=%s agents=%s mb=%s steps=%s\n' \
        "$kag_run" "$kag_h" "$kag_a" "$kag_mb" "$kag_steps"

    ./puffer train kaggriculture \
        "base.run_id=$kag_run" \
        "base.load_model_path=$kag_ckpt" \
        "vec.total_agents=$kag_a" \
        "train.horizon=$kag_h" \
        "train.minibatch_size=$kag_mb" \
        "train.total_timesteps=$kag_steps" \
        > "$kag_log" 2>&1 || {
            printf '  FAILED (see %s)\n' "$kag_log"
            continue
        }

    kag_ini="logs/kaggriculture/${kag_run}.ini"
    kag_sps=$(grep '^SPS = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    kag_score=$(grep '^env/score = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    kag_money=$(grep '^env/money = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    kag_gdp=$(grep '^env/gdp = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$kag_run" "$kag_parent" "$kag_h" "$kag_a" "$kag_mb" \
        "${kag_sps:-NA}" "${kag_score:-NA}" "${kag_money:-NA}" "${kag_gdp:-NA}" >> "$kag_out"
    printf '  sps=%s potential=%s money=%s gdp=%s\n' \
        "${kag_sps:-NA}" "${kag_score:-NA}" "${kag_money:-NA}" "${kag_gdp:-NA}"
done

printf 'Wrote %s\n' "$kag_out"
