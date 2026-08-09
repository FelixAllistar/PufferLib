#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

usage() {
    cat >&2 <<'EOF'
Usage:
  sweep_v2.sh hypers [CHECKPOINT] [TIMESTEPS=25000000] [RUNS=48]
  sweep_v2.sh rewards CHECKPOINT HYPER_INI [TIMESTEPS=25000000] [RUNS=40]

The hyper sweep holds rewards and opponent composition fixed. The reward sweep
loads training hypers from a selected sweep INI and only varies crop-relevant
reward coefficients. Both write a compact ranked TSV next to the raw log.
EOF
    exit 2
}

ini_get() {
    local file=$1 section=$2 key=$3
    awk -v wanted_section="$section" -v wanted_key="$key" '
        $0 == "[" wanted_section "]" { active = 1; next }
        /^\[/ { active = 0 }
        active {
            split($0, pair, "=")
            name = pair[1]
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", name)
            if (name == wanted_key) {
                sub(/^[^=]*=[[:space:]]*/, "", $0)
                sub(/[[:space:]]+$/, "", $0)
                print $0
                exit
            }
        }
    ' "$file"
}

mode=${1:-}
champion=saved/kaggriculture_v2/champion_crop_real_opening.bin
hyper_keys=(
    learning_rate ent_coef gamma gae_lambda horizon minibatch_size clip_coef
    vf_coef vf_clip_coef max_grad_norm emag_kl_coef emag_tau
)
extra_overrides=()

case "$mode" in
    hypers)
        checkpoint=${2:-$champion}
        timesteps=${3:-25000000}
        runs=${4:-48}
        sweep_only='train.learning_rate, train.ent_coef, train.gamma, train.gae_lambda, train.horizon, train.minibatch_size, train.clip_coef, train.vf_coef, train.vf_clip_coef, train.max_grad_norm, train.emag_kl_coef, train.emag_tau'
        ;;
    rewards)
        (($# >= 3 && $# <= 5)) || usage
        checkpoint=$2
        hyper_ini=$3
        timesteps=${4:-25000000}
        runs=${5:-40}
        [[ -f $hyper_ini ]] || { printf 'Hyper INI not found: %s\n' "$hyper_ini" >&2; exit 1; }
        for key in "${hyper_keys[@]}"; do
            value=$(ini_get "$hyper_ini" train "$key")
            [[ -n $value ]] || { printf 'Missing train.%s in %s\n' "$key" "$hyper_ini" >&2; exit 1; }
            extra_overrides+=("train.$key=$value")
        done
        sweep_only='env.reward_potential_scale, env.reward_win, env.reward_seed_value, env.reward_product_value, env.reward_crop_value, env.reward_neglect_discount, env.reward_liquidation_days, env.reward_productive_action, env.reward_inactivity, env.reward_neglect_death'
        ;;
    *) usage ;;
esac

[[ -f $checkpoint ]] || { printf 'Checkpoint not found: %s\n' "$checkpoint" >&2; exit 1; }
[[ $timesteps =~ ^[0-9]+$ && $timesteps -gt 0 ]] || usage
[[ $runs =~ ^[0-9]+$ && $runs -gt 0 ]] || usage

panel_items=(
    "$checkpoint"
    saved/kaggriculture_v2/champion_crop_real_opening.bin
    saved/kaggriculture_v2/real_opening_50m.bin
    saved/kaggriculture_v2/phase3_long_season.bin
    checkpoints/kaggriculture/1785775706210/0000000006553600.bin
    checkpoints/kaggriculture/1785775706210/0000000036044800.bin
)
panel=''
for item in "${panel_items[@]}"; do
    [[ -f $item ]] || continue
    [[ ,$panel, == *,$item,* ]] && continue
    panel+="${panel:+,}$item"
done

mkdir -p logs/kaggriculture
stamp=$(date +%Y%m%d_%H%M%S)
log="logs/kaggriculture/v2_${mode}_sweep_${stamp}.log"
report="logs/kaggriculture/v2_${mode}_sweep_${stamp}.tsv"
marker=$(mktemp)
rows=$(mktemp)
trap 'rm -f "$marker" "$rows"' EXIT

printf 'Starting %s sweep: checkpoint=%s steps=%s runs=%s panel=%s\n' \
    "$mode" "$checkpoint" "$timesteps" "$runs" "$panel"

./puffer sweep kaggriculture \
    "base.load_model_path=$checkpoint" \
    "env.episode_steps=720" \
    "env.bot_opponent_fraction=0.75" \
    "selfplay.opponent_pool=$panel" \
    "selfplay.eval_pool_size=0" \
    "selfplay.eval_games=0" \
    "train.total_timesteps=$timesteps" \
    "sweep.max_runs=$runs" \
    "sweep.sweep_only=$sweep_only" \
    "${extra_overrides[@]}" | tee "$log"

while IFS= read -r ini; do
    suffix=${ini##*_}
    suffix=${suffix%.ini}
    run=$((10#$suffix))
    line=$(grep -E "^sweep run=${run} " "$log" | tail -1 || true)
    [[ -n $line ]] || continue
    score=$(sed -n 's/.* score=\([^ ]*\).*/\1/p' <<<"$line")
    cost=$(sed -n 's/.* cost=\([^ ]*\).*/\1/p' <<<"$line")
    run_id=$(ini_get "$ini" base run_id)
    final=''
    if [[ -d checkpoints/kaggriculture/$run_id ]]; then
        final=$(find "checkpoints/kaggriculture/$run_id" -type f -name '*.bin' \
            -printf '%f %p\n' | sort -r | sed -n '1s/^[^ ]* //p')
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$score" "$cost" "$run" "$run_id" "$ini" "$final" >> "$rows"
done < <(find logs/kaggriculture -maxdepth 1 -type f -name 'sweep_*.ini' \
    -newer "$marker" | sort)

{
    printf 'score\tcost_s\trun\trun_id\tini\tcheckpoint\n'
    sort -t $'\t' -k1,1gr "$rows"
} > "$report"

printf '\nRanked results: %s\n' "$report"
sed -n '1,11p' "$report"
