#!/bin/bash
# Horizon x agents x minibatch capacity sweep.
#
# The built-in `./puffer sweep` cannot express the epoch-sampling divisibility
# constraint: primary_batch % minibatch must be 0, and any nonzero frozen bank
# makes primary_agents a non-power-of-two, so minibatch gets snapped down and
# half the workers fail. This driver sidesteps that by disabling selfplay
# (primary_agents == total_agents, budget is a clean power of two) and only
# emitting (horizon, agents, minibatch) triples where minibatch divides
# agents * horizon, so every point is exactly valid and nothing is snapped.
#
# Horizon list uses the common powers of two plus the full 720-turn episode.
#
# Usage:
#   sweep_horizon_capacity.sh [STEPS]
#   HORIZONS="16 32 64 128 256 512" AGENTS="2048 4096 8192 16384" \
#       MINIBATCH="2048 4096 8192 16384 32768 65536" sweep_horizon_capacity.sh
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_steps=${1:-20000000}
kag_horizons=${HORIZONS:-"16 32 64 128 256 512 720"}
kag_agents=${AGENTS:-"2048 4096 8192 16384"}
kag_minibatch=${MINIBATCH:-"2048 4096 8192 16384 32768 65536"}
kag_out=logs/kaggriculture/horizon_capacity_sweep.tsv

mkdir -p logs/kaggriculture
[[ -f $kag_out ]] || printf 'run\thorizon\tagents\tminibatch\tsps\tpotential\tmoney\tgdp\n' > "$kag_out"

for kag_h in $kag_horizons; do
    for kag_a in $kag_agents; do
        for kag_mb in $kag_minibatch; do
            kag_primary=$((kag_a * kag_h))
            # Minibatch must not exceed the rollout and must divide it so epoch
            # sampling never snaps the requested size.
            if ((kag_mb > kag_primary)); then
                printf 'SKIP %-40s (minibatch %s > primary batch %s)\n' \
                    "h${kag_h}_a${kag_a}_m${kag_mb}" "$kag_mb" "$kag_primary"
                continue
            fi
            if ((kag_primary % kag_mb != 0)); then
                printf 'SKIP %-40s (primary batch %s not divisible by %s)\n' \
                    "h${kag_h}_a${kag_a}_m${kag_mb}" "$kag_primary" "$kag_mb"
                continue
            fi

            kag_run="h${kag_h}_a${kag_a}_m${kag_mb}"
            kag_log="logs/kaggriculture/${kag_run}.log"
            if grep -qF "$kag_run" "$kag_out" 2>/dev/null; then
                printf 'SKIP %-28s (already in %s)\n' "$kag_run" "$kag_out"
                continue
            fi
            printf 'RUN %-28s horizon=%-4s agents=%-5s mb=%-6s\n' \
                "$kag_run" "$kag_h" "$kag_a" "$kag_mb"

            ./puffer train kaggriculture \
                "base.run_id=$kag_run" \
                "vec.total_agents=$kag_a" \
                "vec.num_frozen_banks=1" \
                "vec.frozen_bank_pct=0" \
                "train.horizon=$kag_h" \
                "train.minibatch_size=$kag_mb" \
                "train.total_timesteps=$kag_steps" \
                "selfplay.enabled=0" \
                "selfplay.eval_pool_size=0" \
                > "$kag_log" 2>&1 || {
                    printf '  FAILED (see %s)\n' "$kag_log"
                    continue
                }

            kag_ini="logs/kaggriculture/${kag_run}.ini"
            kag_sps=$(grep '^SPS = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
            kag_score=$(grep '^env/score = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
            kag_money=$(grep '^env/money = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
            kag_gdp=$(grep '^env/gdp = ' "$kag_ini" 2>/dev/null | tail -1 | awk '{print $3}') || true
            kag_sps=${kag_sps:-NA}
            kag_score=${kag_score:-NA}
            kag_money=${kag_money:-NA}
            kag_gdp=${kag_gdp:-NA}
            [[ $kag_sps == NA || $kag_score == NA || $kag_money == NA || $kag_gdp == NA ]] && {
                printf '  INCOMPLETE (sps=%s potential=%s money=%s gdp=%s)\n' \
                    "$kag_sps" "$kag_score" "$kag_money" "$kag_gdp"
            }
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$kag_run" "$kag_h" "$kag_a" "$kag_mb" \
                "$kag_sps" "$kag_score" "$kag_money" "$kag_gdp" >> "$kag_out"
            printf '  sps=%s potential=%s money=%s gdp=%s\n' \
                "$kag_sps" "$kag_score" "$kag_money" "$kag_gdp"
        done
    done
done

printf 'Wrote %s\n' "$kag_out"
if command -v column >/dev/null 2>&1; then
    column -t -s $'\t' "$kag_out"
else
    cat "$kag_out"
fi
