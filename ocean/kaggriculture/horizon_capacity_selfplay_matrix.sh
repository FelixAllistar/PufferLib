#!/bin/bash
# Self-play horizon x agents x minibatch matrix.
#
# The source config is intentionally left in charge of every setting except
# the three requested axes, the unique run id, and the fixed trial length.
# In particular this script does not touch rewards, self-play, frozen banks,
# bot fractions, model/load paths, magnet, or evaluation settings.
#
# Phase 1 runs every valid power-of-two combination for 20M steps. Phase 2
# selects the strongest completed rows and continues each from its final
# checkpoint for another 20M steps.
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_steps=${STEPS:-20000000}
kag_horizons=${HORIZONS:-"16 32 64 128 256 512 720"}
kag_agents=${AGENTS:-"2048 4096 8192 16384"}
kag_minibatch=${MINIBATCH:-"2048 4096 8192 16384 32768 65536"}
kag_top=${TOP_K:-8}
kag_out=logs/kaggriculture/horizon_capacity_selfplay_20m.tsv
kag_stage2=logs/kaggriculture/horizon_capacity_selfplay_20m_stage2.tsv
kag_selection=logs/kaggriculture/horizon_capacity_selfplay_selection.tsv

mkdir -p logs/kaggriculture
[[ -f $kag_out ]] || printf 'run\thorizon\tagents\tminibatch\tsps\tpotential\tmoney\tgdp\n' > "$kag_out"
[[ -f $kag_stage2 ]] || printf 'run\thorizon\tagents\tminibatch\tsps\tpotential\tmoney\tgdp\n' > "$kag_stage2"

run_trial() {
    local kag_run=$1
    local kag_h=$2
    local kag_a=$3
    local kag_mb=$4
    local kag_load=${5:-}
    local kag_dest=$6
    local kag_log="logs/kaggriculture/${kag_run}.log"

    if grep -qF "$kag_run" "$kag_dest" 2>/dev/null; then
        printf 'SKIP %-42s (already recorded)\n' "$kag_run"
        return 0
    fi

    printf 'RUN %-42s horizon=%-4s agents=%-5s mb=%-6s steps=%s\n' \
        "$kag_run" "$kag_h" "$kag_a" "$kag_mb" "$kag_steps"

    local -a kag_cmd=(
        ./puffer train kaggriculture
        "base.run_id=$kag_run"
        "vec.total_agents=$kag_a"
        "train.horizon=$kag_h"
        "train.minibatch_size=$kag_mb"
        "train.total_timesteps=$kag_steps"
    )
    if [[ -n $kag_load ]]; then
        kag_cmd+=("base.load_model_path=$kag_load")
    fi

    "${kag_cmd[@]}" > "$kag_log" 2>&1 || {
        printf '  FAILED (see %s)\n' "$kag_log"
        return 0
    }

    local kag_ini="logs/kaggriculture/${kag_run}.ini"
    local kag_sps kag_score kag_money kag_gdp
    kag_sps=$(grep '^SPS = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    kag_score=$(grep '^env/score = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    kag_money=$(grep '^env/money = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    kag_gdp=$(grep '^env/gdp = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
    kag_sps=${kag_sps:-NA}
    kag_score=${kag_score:-NA}
    kag_money=${kag_money:-NA}
    kag_gdp=${kag_gdp:-NA}
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$kag_run" "$kag_h" "$kag_a" "$kag_mb" \
        "$kag_sps" "$kag_score" "$kag_money" "$kag_gdp" >> "$kag_dest"
    printf '  sps=%s potential=%s money=%s gdp=%s\n' \
        "$kag_sps" "$kag_score" "$kag_money" "$kag_gdp"
}

printf '=== PHASE 1: valid matrix, %s steps each ===\n' "$kag_steps"
for kag_h in $kag_horizons; do
    for kag_a in $kag_agents; do
        for kag_mb in $kag_minibatch; do
            kag_primary=$((kag_a * kag_h))
            if ((kag_mb > kag_primary || kag_primary % kag_mb != 0)); then
                printf 'SKIP h%s_a%s_m%s (invalid full rollout divisibility)\n' \
                    "$kag_h" "$kag_a" "$kag_mb"
                continue
            fi
            kag_run="matrix_sp20m_h${kag_h}_a${kag_a}_m${kag_mb}"
            run_trial "$kag_run" "$kag_h" "$kag_a" "$kag_mb" "" "$kag_out"
        done
    done
done

printf '=== SELECTING TOP %s FOR PHASE 2 ===\n' "$kag_top"
# Rank by a soft geometric health score. GDP=0 cannot win the health branch;
# a small fallback keeps the phase useful if every short trial stalls.
awk -F '\t' '
    NR > 1 && $5 != "NA" && $6 != "NA" && $7 != "NA" && $8 != "NA" {
        p = $6 + 0; m = $7 + 0; g = $8 + 0;
        pn = (p > 3000 ? (p - 3000) / (p + 10000) : 0);
        mn = (m > 3000 ? (m - 3000) / (m + 10000) : 0);
        gn = (g > 0 ? g / (g + 20000) : 0);
        if (pn > 0 && mn > 0 && gn > 0) {
            rank = (pn * mn * gn) ^ (1 / 3);
        } else {
            rank = 0.001 * (pn + mn + gn);
        }
        print rank "\t" $0;
    }
' "$kag_out" | sort -t $'\t' -k1,1nr | head -n "$kag_top" > "$kag_selection"

if [[ -s $kag_selection ]]; then
    while IFS=$'\t' read -r kag_rank kag_run kag_h kag_a kag_mb kag_sps kag_score kag_money kag_gdp; do
        kag_ckpt=$(find "checkpoints/kaggriculture/$kag_run" -maxdepth 1 -type f -name '*.bin' \
            -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -1 | cut -d' ' -f2- || true)
        if [[ -z $kag_ckpt || ! -f $kag_ckpt ]]; then
            printf 'SKIP %s (no final checkpoint found)\n' "$kag_run"
            continue
        fi
        kag_stage_run="${kag_run}_stage2"
        run_trial "$kag_stage_run" "$kag_h" "$kag_a" "$kag_mb" "$kag_ckpt" "$kag_stage2"
    done < "$kag_selection"
else
    printf 'No completed non-NA rows were available for phase 2.\n'
fi

printf 'Wrote %s and %s\n' "$kag_out" "$kag_stage2"
