#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# < 1 || $# > 3)); then
    echo "Usage: $0 RUN_DIR [GAMES] [OUTPUT.tsv]" >&2
    exit 2
fi

kag_run=$1
kag_games=${2:-100}
kag_output=${3:-logs/kaggriculture/top_checkpoint_eval.tsv}
kag_seed_a=${KAG_FIXED_SEED_A:-8811}
kag_seed_b=${KAG_FIXED_SEED_B:-9911}
if ! [[ $kag_games =~ ^[0-9]+$ ]] || ((kag_games < 2 || kag_games % 2)); then
    echo "GAMES must be an even integer of at least 2" >&2
    exit 2
fi
if ! [[ $kag_seed_a =~ ^[0-9]+$ && $kag_seed_b =~ ^[0-9]+$ ]]; then
    echo "KAG_FIXED_SEED_A/B must be nonnegative integers" >&2
    exit 2
fi
if [[ ! -d $kag_run ]]; then
    echo "Checkpoint directory not found: $kag_run" >&2
    exit 1
fi

mapfile -t kag_models < <(find "$kag_run" -maxdepth 1 -type f \
    -regextype posix-extended -regex '.*/[0-9]{16}\.bin' -print | sort)
if [[ -n ${KAG_BASE_MODEL:-} ]]; then
    kag_models=("$KAG_BASE_MODEL" "${kag_models[@]}")
fi
if ((${#kag_models[@]} == 0)); then
    echo "No numeric checkpoints under $kag_run" >&2
    exit 1
fi

kag_json() {
    local kag_text=$1 kag_key=$2
    printf '%s\n' "$kag_text" \
        | grep -o '"'"$kag_key"'"[:][0-9.eE+-]*' \
        | tail -n 1 | cut -d: -f2
}

kag_half=$((kag_games / 2))
kag_tmp=$(mktemp)
trap 'rm -f "$kag_tmp"' EXIT
printf 'policy\twin\tdraw\tmoney\ttop_money\n' > "$kag_tmp"

for kag_model in "${kag_models[@]}"; do
    kag_forward=$(./puffer eval_bot kaggriculture \
        "base.eval_episodes=$kag_half" "base.eval_agents=$kag_games" \
        "base.seed=$kag_seed_a" "base.load_model_path=$kag_model" \
        env.reset_opening_turns=0 env.reset_opening_min=0 \
        env.reset_opening_prob=0 env.opening_turns=0 \
        env.bot_opponent_fraction=1 env.bot_top_fraction=1 \
        env.bot_pass_fraction=0 env.bot_rules_fraction=0 \
        env.bot_script_fraction=0 env.bot_adaptive_fraction=0 \
        env.bot_first=0 selfplay.enabled=0 train.total_timesteps=0 2>&1)
    kag_reverse=$(./puffer eval_bot kaggriculture \
        "base.eval_episodes=$kag_half" "base.eval_agents=$kag_games" \
        "base.seed=$kag_seed_b" "base.load_model_path=$kag_model" \
        env.reset_opening_turns=0 env.reset_opening_min=0 \
        env.reset_opening_prob=0 env.opening_turns=0 \
        env.bot_opponent_fraction=1 env.bot_top_fraction=1 \
        env.bot_pass_fraction=0 env.bot_rules_fraction=0 \
        env.bot_script_fraction=0 env.bot_adaptive_fraction=0 \
        env.bot_first=1 selfplay.enabled=0 train.total_timesteps=0 2>&1)

    kag_fw=$(kag_json "$kag_forward" env/perf)
    kag_fd=$(kag_json "$kag_forward" env/draw_rate)
    kag_fm=$(kag_json "$kag_forward" env/money)
    kag_ft=$(kag_json "$kag_forward" env/opponent_money)
    kag_rw=$(kag_json "$kag_reverse" env/perf)
    kag_rd=$(kag_json "$kag_reverse" env/draw_rate)
    kag_rm=$(kag_json "$kag_reverse" env/money)
    kag_rt=$(kag_json "$kag_reverse" env/opponent_money)
    if [[ -z $kag_fw || -z $kag_fd || -z $kag_fm || -z $kag_ft \
            || -z $kag_rw || -z $kag_rd || -z $kag_rm || -z $kag_rt ]]; then
        echo "Failed to parse eval for $kag_model" >&2
        printf '%s\n%s\n' "$kag_forward" "$kag_reverse" >&2
        exit 1
    fi
    read -r kag_win kag_draw kag_money kag_top_money < <(awk \
        -v fw="$kag_fw" -v rw="$kag_rw" -v fd="$kag_fd" -v rd="$kag_rd" \
        -v fm="$kag_fm" -v rm="$kag_rm" -v ft="$kag_ft" -v rt="$kag_rt" \
        'BEGIN {printf "%.6f %.6f %.3f %.3f\n", (fw+rw)/2, (fd+rd)/2, (fm+rm)/2, (ft+rt)/2}')
    printf '%s\t%s\t%s\t%s\t%s\n' "$kag_model" "$kag_win" \
        "$kag_draw" "$kag_money" "$kag_top_money" >> "$kag_tmp"
    printf '%-72s win=%7.4f money=%9.1f\n' "$kag_model" \
        "$kag_win" "$kag_money"
done

mkdir -p "$(dirname "$kag_output")"
{
    head -n 1 "$kag_tmp"
    tail -n +2 "$kag_tmp" | sort -t$'\t' -k4,4gr
} > "$kag_output"
echo "Ranked root-only results: $kag_output"
cat "$kag_output"
