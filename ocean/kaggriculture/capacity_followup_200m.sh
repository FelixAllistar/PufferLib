#!/bin/bash
# Four-candidate 200M follow-up for the horizon/capacity screen.
#
# This is intentionally an explicit list, not a Cartesian sweep: each trial
# keeps the same remote config and varies only horizon, agents, and minibatch.
# It starts cold, keeps the configured frozen-bank self-play mixture, and
# records a separate TSV.
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_steps=${1:-200000000}
kag_out=logs/kaggriculture/horizon_capacity_followup_200m.tsv
mkdir -p logs/kaggriculture
[[ -f $kag_out ]] || printf 'run\thorizon\tagents\tminibatch\tsps\tpotential\tmoney\tgdp\n' > "$kag_out"

# Best money/health/throughput points from horizon_capacity_sweep.tsv.
kag_specs=(
    "h32_a2048_m4096 32 2048 4096"
    "h32_a4096_m8192 32 4096 8192"
    "h16_a8192_m8192 16 8192 8192"
    "h64_a2048_m8192 64 2048 8192"
)

for kag_spec in "${kag_specs[@]}"; do
    read -r kag_label kag_h kag_a kag_mb <<< "$kag_spec"
    kag_run="${kag_label}_200m"
    kag_log="logs/kaggriculture/${kag_run}.log"
    if grep -qF "$kag_run" "$kag_out" 2>/dev/null; then
        printf 'SKIP %-28s (already in %s)\n' "$kag_run" "$kag_out"
        continue
    fi

    printf 'RUN %-28s horizon=%-4s agents=%-5s mb=%-6s steps=%s\n' \
        "$kag_run" "$kag_h" "$kag_a" "$kag_mb" "$kag_steps"

    ./puffer train kaggriculture \
        "base.run_id=$kag_run" \
        "base.load_model_path=None" \
        "vec.total_agents=$kag_a" \
        "vec.num_frozen_banks=4" \
        "vec.frozen_bank_pct=0.75" \
        "vec.frozen_bank_hidden_size=512" \
        "vec.frozen_bank_num_layers=2" \
        "train.horizon=$kag_h" \
        "train.minibatch_size=$kag_mb" \
        "train.total_timesteps=$kag_steps" \
        "selfplay.enabled=1" \
        "selfplay.max_size=16" \
        "selfplay.snapshot_interval=0" \
        "selfplay.eval_pool_size=1" \
        "selfplay.opponent_league=None" \
        "selfplay.opponent_pool=None" \
        "selfplay.opponent_pool_weights=None" \
        "selfplay.opponent_pool_prob=0" \
        "selfplay.magnet_path=None" \
        "env.bot_opponent_fraction=0.5" \
        "env.bot_pass_fraction=0.1" \
        "env.bot_first=0" \
        "env.bot_top_fraction=0.1" \
        "env.bot_rules_fraction=0.1" \
        "env.bot_script_fraction=0.1" \
        "env.bot_adaptive_fraction=0.1" \
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
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$kag_run" "$kag_h" "$kag_a" "$kag_mb" \
        "$kag_sps" "$kag_score" "$kag_money" "$kag_gdp" >> "$kag_out"
    printf '  sps=%s potential=%s money=%s gdp=%s\n' \
        "$kag_sps" "$kag_score" "$kag_money" "$kag_gdp"
done

printf 'Wrote %s\n' "$kag_out"
if command -v column >/dev/null 2>&1; then
    column -t -s $'\t' "$kag_out"
else
    cat "$kag_out"
fi
