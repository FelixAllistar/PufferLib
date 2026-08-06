#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

run=${1:?usage: eval_v4.sh RUN_OR_DIRECTORY [games]}
games=${2:-50}
if [[ -d $run ]]; then
    directory=$run
else
    directory="checkpoints/kaggriculture/$run"
fi
[[ -d $directory ]] || { printf 'Checkpoint directory not found: %s\n' "$directory" >&2; exit 1; }
[[ -x ./kaggriculture ]] || { printf 'Build ./kaggriculture first\n' >&2; exit 1; }

steps=$((games * 720))
report="logs/kaggriculture/v4_eval_$(basename "$directory")_$(date +%Y%m%d_%H%M%S).tsv"
mkdir -p "$(dirname "$report")"
printf 'checkpoint\trules_money\tpass_money\trules_orders_turn\trules_buy\trules_seed_buy\trules_product_buy\trules_animal_buy\trules_sell\trules_hire\trules_land\trules_animal_place\trules_feed\trules_care\trules_animal_harvest\trules_fert_collect\n' > "$report"

parse_metric() {
    local text=$1 key=$2
    sed -n "s|.*[[:space:]]${key}=\\([0-9.]*\\).*|\\1|p" <<< "$text" | head -n1
}

while IFS= read -r checkpoint; do
    rules=$(./kaggriculture bench "$steps" "$checkpoint" rules)
    passive=$(./kaggriculture bench "$steps" "$checkpoint" pass)
    rules_money=$(sed -n 's/.*avg_money=(\([0-9-]*\),.*/\1/p' <<< "$rules" | head -n1)
    pass_money=$(sed -n 's/.*avg_money=(\([0-9-]*\),.*/\1/p' <<< "$passive" | head -n1)
    action_line=$(sed -n 's/^P0 /P0 /p' <<< "$rules" | head -n1)
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$checkpoint" "$rules_money" "$pass_money" \
        "$(parse_metric "$action_line" 'orders/turn')" \
        "$(parse_metric "$action_line" 'buy')" \
        "$(parse_metric "$action_line" 'seed_buy')" \
        "$(parse_metric "$action_line" 'product_buy')" \
        "$(parse_metric "$action_line" 'animal_buy')" \
        "$(parse_metric "$action_line" 'sell')" \
        "$(parse_metric "$action_line" 'hire')" \
        "$(parse_metric "$action_line" 'land')" \
        "$(parse_metric "$action_line" 'animal_place')" \
        "$(parse_metric "$action_line" 'feed')" \
        "$(parse_metric "$action_line" 'care')" \
        "$(parse_metric "$action_line" 'animal_harvest')" \
        "$(parse_metric "$action_line" 'fert_collect')" \
        | tee -a "$report"
done < <(find "$directory" -maxdepth 1 -type f -name '*.bin' | sort)

printf '\nRanked by conservative money (minimum of rules/pass):\n'
awk -F '\t' 'NR > 1 {m=$2<$3?$2:$3; print m "\t" $0}' "$report" \
    | sort -t $'\t' -k1,1nr | head -n 12
printf 'Wrote %s\n' "$report"
