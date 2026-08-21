#!/bin/bash
# Capacity sweep: vec.total_agents x train.minibatch_size x train.learning_rate
#
# The built-in `./puffer sweep` cannot express the epoch-sampling divisibility
# constraint: primary_batch % minibatch must be 0, and any nonzero frozen bank
# makes primary_agents a non-power-of-two, so minibatch gets snapped down to
# awkward sizes and half the workers fail. This driver sidesteps that by
# disabling selfplay (primary_agents == total_agents, a clean power of two) and
# deriving minibatch as agents * factor, so every point is exactly valid.
#
# Ranking is SPS + dashboard potential from a short run. Cash and GDP are
# recorded beside it so a high-potential, low-cash farm remains diagnosable.
# Re-validate the winner
# with selfplay/league enabled before committing to a long run.
#
# Usage:
#   sweep_capacity.sh [STEPS]
#   AGENTS="2048 4096 8192 16384" MINIBATCH="16384 32768 65536" \
#       LR="0.0003 0.00055 0.001" sweep_capacity.sh 50000000
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_steps=${1:-20000000}
kag_agents=${AGENTS:-"2048 4096 8192 16384"}
kag_minibatch=${MINIBATCH:-"16384 32768 65536"}
kag_lr=${LR:-"0.0003 0.00055 0.001"}
kag_out=logs/kaggriculture/capacity_sweep.tsv

mkdir -p logs/kaggriculture
[[ -f $kag_out ]] || printf 'run\tagents\tminibatch\tlr\tsps\tpotential\tmoney\tgdp\n' > "$kag_out"

for kag_a in $kag_agents; do
    for kag_mb in $kag_minibatch; do
        for kag_l in $kag_lr; do
            # Reject absolute minibatches larger than the rollout itself.
            ((kag_mb <= kag_a * 256)) || { printf 'SKIP %s mb>agents*256\n' \
                "cap_a${kag_a}_m${kag_mb}_lr${kag_l}" >&2; continue; }
            kag_lr_label=$(printf '%s' "$kag_l" | tr '.' '_')
            kag_run="cap_a${kag_a}_m${kag_mb}_lr${kag_lr_label}"
            kag_log="logs/kaggriculture/${kag_run}.log"
            if grep -qF "$kag_run" "$kag_out" 2>/dev/null; then
                printf 'SKIP %-28s (already in %s)\n' "$kag_run" "$kag_out"
                continue
            fi
            printf 'RUN %-28s agents=%-5s mb=%-6s lr=%s\n' \
                "$kag_run" "$kag_a" "$kag_mb" "$kag_l"

            ./puffer train kaggriculture \
                "base.run_id=$kag_run" \
                "vec.total_agents=$kag_a" \
                "vec.num_frozen_banks=1" \
                "vec.frozen_bank_pct=0" \
                "train.minibatch_size=$kag_mb" \
                "train.learning_rate=$kag_l" \
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
                "$kag_run" "$kag_a" "$kag_mb" "$kag_l" \
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
