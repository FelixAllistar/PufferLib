#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_games=20
kag_jobs=4
kag_gpu_agents=${KAG_GPU_AGENTS:-64}
kag_output=logs/kaggriculture/policy_profile_gpu
kag_eval_deterministic=0
kag_opponent=rules
kag_inputs=()

usage() {
    printf '%s\n' \
        "Usage: $0 [options] [CHECKPOINT_OR_DIRECTORY ...]" \
        "  --games N          Even games per policy (default 20)" \
        "  --jobs N           Concurrent CUDA evaluators (default 4)" \
        "  --gpu-agents N     Agents per evaluator (default 64)" \
        "  --output PREFIX    Output TSV prefix" \
        "  --opponent NAME    Fixed opponent: pass or rules (default rules)" \
        "  --deterministic    Use masked argmax actions" \
        "  --stochastic       Sample masked actions (default)" \
        "The report is GPU-native and records economic/maintenance counters."
}

while (($#)); do
    case "$1" in
        --games) kag_games=$2; shift 2 ;;
        --jobs) kag_jobs=$2; shift 2 ;;
        --gpu-agents) kag_gpu_agents=$2; shift 2 ;;
        --output) kag_output=$2; shift 2 ;;
        --opponent) kag_opponent=$2; shift 2 ;;
        --deterministic) kag_eval_deterministic=1; shift ;;
        --stochastic) kag_eval_deterministic=0; shift ;;
        -h|--help) usage; exit 0 ;;
        --*) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
        *) kag_inputs+=("$1"); shift ;;
    esac
done

[[ $kag_games =~ ^[0-9]+$ ]] && ((kag_games >= 2 && kag_games % 2 == 0)) \
    || { printf '%s\n' '--games must be an even integer >= 2' >&2; exit 2; }
[[ $kag_jobs =~ ^[0-9]+$ ]] && ((kag_jobs >= 1)) \
    || { printf '%s\n' '--jobs must be >= 1' >&2; exit 2; }
[[ $kag_gpu_agents =~ ^[0-9]+$ ]] && ((kag_gpu_agents >= 4)) \
    || { printf '%s\n' '--gpu-agents must be >= 4' >&2; exit 2; }
[[ $kag_opponent == pass || $kag_opponent == rules ]] \
    || { printf '%s\n' '--opponent must be pass or rules' >&2; exit 2; }
[[ -x ./puffer ]] || { printf '%s\n' 'Build ./puffer first' >&2; exit 1; }
if ((${#kag_inputs[@]} == 0)); then
    kag_inputs=(saved/kaggriculture_league_v5)
fi

declare -A kag_seen=() kag_paths=() kag_roles=() kag_weights=()
kag_order=()
kag_add() {
    local path=$1 base label
    [[ -f $path && $path == *.bin ]] || return
    base=${path##*/}; label=${base%.bin}
    [[ $label =~ ^[0-9]{16}$ ]] && return
    [[ -n ${kag_seen["$path"]+x} ]] && return
    kag_seen["$path"]=1; kag_paths["$label"]=$path; kag_order+=("$label")
}
for input in "${kag_inputs[@]}"; do
    if [[ -f $input ]]; then
        kag_add "$input"
    elif [[ -d $input ]]; then
        while IFS= read -r path; do kag_add "$path"; done \
            < <(find "$input" -type f -name '*.bin' -print | sort)
        if [[ -f $input/manifest.tsv ]]; then
            while IFS=$'\t' read -r policy role weight _; do
                [[ $policy == policy ]] && continue
                kag_roles["$policy"]=$role; kag_weights["$policy"]=$weight
            done < "$input/manifest.tsv"
        fi
    else
        printf 'Input does not exist: %s\n' "$input" >&2; exit 1
    fi
done
(( ${#kag_order[@]} >= 1 )) || { printf '%s\n' 'No named checkpoints found' >&2; exit 1; }
(( ${#kag_order[@]} <= 64 )) || { printf 'Refusing %d policies\n' "${#kag_order[@]}" >&2; exit 1; }

kag_tmp=$(mktemp -d)
trap 'rm -r "$kag_tmp"' EXIT
mkdir -p "$(dirname "$kag_output")"

json_value() {
    local text=$1 key=$2
    printf '%s\n' "$text" \
        | grep -o '"'"$key"'"[:][0-9.eE+-]*' \
        | tail -n 1 | cut -d: -f2
}

profile_one() {
    local label=$1 path=${kag_paths["$1"]} text json completed
    local architecture_args=()
    local pass_fraction=0 rules_fraction=0
    if [[ $kag_opponent == pass ]]; then
        pass_fraction=1
    else
        rules_fraction=1
    fi
    if [[ $label =~ _([0-9]+)x([0-9]+)_ ]]; then
        architecture_args+=(
            "policy.hidden_size=${BASH_REMATCH[1]}"
            "policy.num_layers=${BASH_REMATCH[2]}"
        )
    fi
    if ! text=$(./puffer eval_bot kaggriculture \
            "base.eval_episodes=$((kag_games / 2))" \
            "base.eval_agents=$kag_gpu_agents" \
            "base.eval_deterministic=$kag_eval_deterministic" \
            "base.load_model_path=$path" \
            "${architecture_args[@]}" \
            env.bot_opponent_fraction=1 "env.bot_pass_fraction=$pass_fraction" \
            "env.bot_rules_fraction=$rules_fraction" env.bot_script_fraction=0 \
            env.bot_adaptive_fraction=0 selfplay.enabled=0 \
            train.total_timesteps=0 2>&1); then
        printf 'GPU profile failed for %s\n' "$label" >&2; return 1
    fi
    json=$(printf '%s\n' "$text" | grep '"env/perf"' | tail -n 1)
    [[ -n $json ]] || { printf 'GPU profile parse failed for %s\n' "$label" >&2; return 1; }
    completed=$(json_value "$json" env/n)
    [[ -n $completed ]] || { printf 'GPU profile missing env/n for %s\n' "$label" >&2; return 1; }
    printf '%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$label" "${kag_roles[$label]:-unassigned}" "${kag_weights[$label]:-0}" \
        "$completed" \
        "$(json_value "$json" env/perf)" "$(json_value "$json" env/draw_rate)" \
        "$(json_value "$json" env/future_value_score)" "$(json_value "$json" env/opponent_future_value_score)" \
        "$(json_value "$json" env/money)" "$(json_value "$json" env/opponent_money)" \
        "$(json_value "$json" env/gdp)" "$(json_value "$json" env/opponent_gdp)" \
        "$(json_value "$json" env/production_units)" "$(json_value "$json" env/opponent_production_units)" \
        "$(json_value "$json" env/crop_production_units)" \
        "$(json_value "$json" env/animal_production_units)" \
        "$(json_value "$json" env/successful_plants)" \
        "$(json_value "$json" env/successful_animal_places)" \
        "$(json_value "$json" env/sold_units)" "$(json_value "$json" env/sales_revenue)" \
        "$(json_value "$json" env/bought_units)" "$(json_value "$json" env/purchase_spend)" \
        "$(json_value "$json" env/crop_sold_units)" \
        "$(json_value "$json" env/crop_sales_revenue)" \
        "$(json_value "$json" env/animal_product_sold_units)" \
        "$(json_value "$json" env/animal_product_sales_revenue)" \
        "$(json_value "$json" env/strawberry_sold_units)" \
        "$(json_value "$json" env/strawberry_sales_revenue)" \
        "$(json_value "$json" env/milk_sold_units)" \
        "$(json_value "$json" env/milk_sales_revenue)" \
        "$(json_value "$json" env/ending_shed_units)" \
        "$(json_value "$json" env/ending_shed_value)" \
        "$(json_value "$json" env/carrot_opportunity_fraction)" \
        "$(json_value "$json" env/carrot_opportunity_no_production_price)" \
        "$(json_value "$json" env/carrot_opportunity_response)" \
        "$(json_value "$json" env/carrot_opportunity_production)" \
        "$(json_value "$json" env/carrot_nonopportunity_production)" \
        "$(json_value "$json" env/carrot_opportunity_sold_units)" \
        "$(json_value "$json" env/carrot_opportunity_sales_revenue)" \
        "$(json_value "$json" env/carrot_opportunity_sale_price)" \
        "$(json_value "$json" env/tomato_opportunity_fraction)" \
        "$(json_value "$json" env/tomato_opportunity_no_production_price)" \
        "$(json_value "$json" env/tomato_opportunity_response)" \
        "$(json_value "$json" env/tomato_opportunity_production)" \
        "$(json_value "$json" env/tomato_nonopportunity_production)" \
        "$(json_value "$json" env/tomato_opportunity_sold_units)" \
        "$(json_value "$json" env/tomato_opportunity_sales_revenue)" \
        "$(json_value "$json" env/tomato_opportunity_sale_price)" \
        "$(json_value "$json" env/egg_opportunity_fraction)" \
        "$(json_value "$json" env/egg_opportunity_no_production_price)" \
        "$(json_value "$json" env/egg_opportunity_response)" \
        "$(json_value "$json" env/egg_opportunity_production)" \
        "$(json_value "$json" env/egg_nonopportunity_production)" \
        "$(json_value "$json" env/egg_opportunity_sold_units)" \
        "$(json_value "$json" env/egg_opportunity_sales_revenue)" \
        "$(json_value "$json" env/egg_opportunity_sale_price)" \
        "$(json_value "$json" env/strawberry_units)" "$(json_value "$json" env/opponent_strawberry_units)" \
        "$(json_value "$json" env/strawberry_value)" "$(json_value "$json" env/opponent_strawberry_value)" \
        "$(json_value "$json" env/milk_units)" "$(json_value "$json" env/opponent_milk_units)" \
        "$(json_value "$json" env/milk_value)" "$(json_value "$json" env/opponent_milk_value)" \
        "$(json_value "$json" env/water_coverage)" "$(json_value "$json" env/neglect_deaths)" \
        "$(json_value "$json" env/planting_day_deaths)" "$(json_value "$json" env/plants_alive)" \
        "$(json_value "$json" env/animals_alive)" "$(json_value "$json" env/weeds)" \
        "$(json_value "$json" env/land_purchases)" \
        "$(json_value "$json" env/productive_extra_tiles)" \
        "$(json_value "$json" env/animal_place_actions)" \
        "$(json_value "$json" env/animal_feed_actions)" \
        "$(json_value "$json" env/animal_care_actions)" \
        "$(json_value "$json" env/animal_harvest_actions)" \
        "$(json_value "$json" env/fertilizer_collect_actions)" \
        "$(json_value "$json" env/orders_per_turn)" "$(json_value "$json" env/buy_orders)" \
        "$(json_value "$json" env/sell_orders)" "$(json_value "$json" env/hire_orders)" \
        > "$kag_tmp/$label.tsv"
}

active=0
wait_slot() {
    while ((active >= kag_jobs)); do wait -n || true; active=$((active - 1)); done
}
printf 'Profiling %d policies on CUDA: %d games/policy, %d jobs\n' \
    "${#kag_order[@]}" "$kag_games" "$kag_jobs"
for label in "${kag_order[@]}"; do
    wait_slot; profile_one "$label" & active=$((active + 1))
done
wait

{
    printf 'policy\trole\tbase_weight\tgames_completed\twin_rate\tdraw_rate\tfuture_value_score\topponent_future_value_score\tmean_money\topponent_money\tgdp\topponent_gdp\tproduction_units\topponent_production_units\tcrop_production_units\tanimal_production_units\tsuccessful_plants\tsuccessful_animal_places\tsold_units\tsales_revenue\tbought_units\tpurchase_spend\tcrop_sold_units\tcrop_sales_revenue\tanimal_product_sold_units\tanimal_product_sales_revenue\tstrawberry_sold_units\tstrawberry_sales_revenue\tmilk_sold_units\tmilk_sales_revenue\tending_shed_units\tending_shed_value\tcarrot_opportunity_fraction\tcarrot_opportunity_no_production_price\tcarrot_opportunity_response\tcarrot_opportunity_production\tcarrot_nonopportunity_production\tcarrot_opportunity_sold_units\tcarrot_opportunity_sales_revenue\tcarrot_opportunity_sale_price\ttomato_opportunity_fraction\ttomato_opportunity_no_production_price\ttomato_opportunity_response\ttomato_opportunity_production\ttomato_nonopportunity_production\ttomato_opportunity_sold_units\ttomato_opportunity_sales_revenue\ttomato_opportunity_sale_price\tegg_opportunity_fraction\tegg_opportunity_no_production_price\tegg_opportunity_response\tegg_opportunity_production\tegg_nonopportunity_production\tegg_opportunity_sold_units\tegg_opportunity_sales_revenue\tegg_opportunity_sale_price\tstrawberry_units\topponent_strawberry_units\tstrawberry_value\topponent_strawberry_value\tmilk_units\topponent_milk_units\tmilk_value\topponent_milk_value\twater_coverage\tneglect_deaths\tplanting_day_deaths\tplants_alive\tanimals_alive\tweeds\tland_purchases\tproductive_extra_tiles\tanimal_place_actions\tanimal_feed_actions\tanimal_care_actions\tanimal_harvest_actions\tfertilizer_collect_actions\torders_per_turn\tbuy_orders\tsell_orders\thire_orders\n'
    for label in "${kag_order[@]}"; do cat "$kag_tmp/$label.tsv"; done
} > "${kag_output}.tsv"
printf 'Wrote %s.tsv\n' "$kag_output"
