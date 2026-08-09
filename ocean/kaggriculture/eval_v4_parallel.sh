#!/bin/bash
set -uo pipefail

cd "$(dirname "$0")/../.."

run=${1:?usage: eval_v4_parallel.sh RUN_OR_DIRECTORY [games] [jobs]}
games=${2:-20}
jobs=${3:-4}
sample=${4:-6}
gpu_agents=${KAG_GPU_AGENTS:-64}
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

bench_one() {
    local checkpoint=$1
    local out
    local json
    json=$(./puffer eval kaggriculture \
        "base.load_model_path=$checkpoint" \
        "base.eval_episodes=$games" \
        "base.eval_agents=$gpu_agents" \
        "selfplay.eval_pool_size=0" \
        "train.total_timesteps=0" \
        2>&1 | grep '"env/perf"' | tail -1)
    local perf score opp
    perf=$(grep -o '"env/perf":[0-9.]*' <<< "$json" | head -1 | cut -d: -f2)
    score=$(grep -o '"env/score":[0-9.]*' <<< "$json" | head -1 | cut -d: -f2)
    opp=$(grep -o '"env/opponent_score":[0-9.]*' <<< "$json" | head -1 | cut -d: -f2)
    printf '%s\t%s\t%s\t%s\n' "$checkpoint" "$score" "$opp" "$perf" \
        > "$(dirname "$report")/.tmp_$(basename "$checkpoint").tsv"
}

mapfile -t all < <(find "$directory" -maxdepth 1 -type f -name '*.bin' -print | sort)
if (( sample > 0 && ${#all[@]} > sample )); then
    checkpoints=()
    for ((i=0; i<sample; i++)); do
        idx=$((i * (${#all[@]} - 1) / (sample - 1)))
        checkpoints+=("${all[idx]}")
    done
else
    checkpoints=("${all[@]}")
fi
printf 'Sampling %d of %d checkpoints (%d games, %d jobs)\n' \
    "${#checkpoints[@]}" "${#all[@]}" "$games" "$jobs"
printf 'checkpoint\tscore\topponent_score\tperf\n' > "$report"
for checkpoint in "${checkpoints[@]}"; do
    while (( $(jobs -rp | wc -l) >= jobs )); do wait -n || true; done
    bench_one "$checkpoint" &
done
wait
for f in "$(dirname "$report")"/.tmp_*.tsv; do cat "$f" >> "$report"; rm -f "$f"; done

printf '\nRanked by score (win rate vs mixed bots):\n'
awk -F '\t' 'NR > 1 {print $4 "\t" $0}' "$report" \
    | sort -t $'\t' -k1,1nr | head -n 12
printf 'Wrote %s\n' "$report"
