#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# < 2 || $# > 3)); then
    echo "Usage: $0 opening|recovery|full CHECKPOINT [GAMES]" >&2
    exit 2
fi

kag_mode=$1
kag_model=$2
kag_games=${3:-64}
[[ -f $kag_model ]] || { echo "Checkpoint not found: $kag_model" >&2; exit 1; }

kag_value() {
    local text=$1 key=$2
    printf '%s\n' "$text" | grep -o '"'"$key"'"[:][0-9.eE+-]*' \
        | tail -n 1 | cut -d: -f2
}

kag_check() {
    local seat=$1 steps=$2
    local out
    out=$(./puffer eval_bot kaggriculture \
        "base.eval_episodes=$kag_games" "base.eval_agents=$((2*kag_games))" \
        "base.load_model_path=$kag_model" "env.episode_steps=$steps" \
        env.opening_turns=0 env.reset_opening_turns=0 \
        env.reset_opening_prob=0 env.bot_opponent_fraction=1 \
        env.bot_pass_fraction=1 env.bot_top_fraction=0 \
        env.bot_rules_fraction=0 env.bot_script_fraction=0 \
        env.bot_adaptive_fraction=0 "env.bot_first=$seat" \
        selfplay.enabled=0 train.total_timesteps=0 2>&1)
    local animals buys places feeds money
    animals=$(kag_value "$out" env/animals_alive)
    buys=$(kag_value "$out" env/animal_buy_orders)
    places=$(kag_value "$out" env/animal_place_actions)
    feeds=$(kag_value "$out" env/animal_feed_actions)
    money=$(kag_value "$out" env/money)
    if [[ -z $animals || -z $buys || -z $places || -z $feeds || -z $money ]]; then
        printf '%s\n' "$out" >&2
        echo "Failed to parse BC gate" >&2
        exit 1
    fi
    printf 'gate=%s seat=%s steps=%s animals=%s buys=%s places=%s feeds=%s money=%s\n' \
        "$kag_mode" "$seat" "$steps" "$animals" "$buys" "$places" \
        "$feeds" "$money"
    if [[ $kag_mode == opening ]]; then
        awk -v a="$animals" -v b="$buys" -v p="$places" \
            'BEGIN {exit !(a >= 4 && b >= 1.5 && p >= 4)}'
    elif [[ $kag_mode == recovery ]]; then
        awk -v a="$animals" -v f="$feeds" -v m="$money" \
            'BEGIN {exit !(a >= 3 && f >= 8 && m >= 0)}'
    else
        awk -v a="$animals" -v f="$feeds" -v m="$money" \
            'BEGIN {exit !(a >= 2 && f >= 3 && m >= 1000)}'
    fi
}

if [[ $kag_mode == opening ]]; then
    kag_check 0 27
    kag_check 1 27
elif [[ $kag_mode == recovery ]]; then
    kag_check 0 96
    kag_check 1 96
elif [[ $kag_mode == full ]]; then
    kag_check 0 720
    kag_check 1 720
else
    echo "Mode must be opening, recovery, or full" >&2
    exit 2
fi

echo "BC $kag_mode closed-loop gate: PASS"
