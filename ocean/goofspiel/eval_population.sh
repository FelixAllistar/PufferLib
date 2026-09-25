#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

prefix=${1:-population64x1}
games=${2:-262144}
threshold=${3:-0.002}
mode=${4:-best}
shift "$(( $# < 4 ? $# : 4 ))"
binary=${GOOFSPIEL_TRAIN_BINARY:-./puffer_goofspiel}
solver=${GOOFSPIEL_EXACT_GPU:-./ocean/goofspiel/build/exploit_gpu}
checkpoint_dir=${GOOFSPIEL_CHECKPOINT_DIR:-checkpoints}
log_dir=${GOOFSPIEL_LOG_DIR:-logs}
[[ "$prefix" =~ ^[a-zA-Z0-9_-]+$ && "$games" =~ ^[1-9][0-9]*$ ]]
[[ "$threshold" =~ ^[0-9]+([.][0-9]+)?$ ]]
shopt -s nullglob
settings=(--policy.hidden_size=64 --policy.num_layers=1 "$@")

case "$mode" in
    best) suffix="" ;;
    all|scan) suffix="_all" ;;
    jsd) suffix="_behavior" ;;
    *) echo "usage: $0 [prefix] [games] [cycle_threshold] [best|all|scan|jsd]" >&2; exit 2 ;;
esac

mkdir -p "$log_dir/goofspiel"
manifest="$log_dir/goofspiel/${prefix}${suffix}_manifest.tsv"
matrix="$log_dir/goofspiel/${prefix}${suffix}_payoff.tsv"
draws="$log_dir/goofspiel/${prefix}${suffix}_draws.tsv"
cycles="$log_dir/goofspiel/${prefix}${suffix}_cycles.tsv"
dirs=("$checkpoint_dir/goofspiel/${prefix}"_seed*)

if [ "${#dirs[@]}" -eq 0 ]; then
    echo "No runs found for prefix: $prefix" >&2
    exit 1
fi
[[ -x "$solver" ]]
if [[ "$mode" = best || "$mode" = all ]]; then
    [[ -x "$binary" ]]
fi

names=()
paths=()
behavior_paths=()
printf 'run\texploitability\tcheckpoint\n' > "$manifest"
for dir in "${dirs[@]}"; do
    run=${dir##*/}
    files=("$dir"/*.bin)
    if [ "${#files[@]}" -eq 0 ]; then
        echo "No checkpoints found in $dir" >&2
        exit 1
    fi
    if [ "$mode" = jsd ]; then
        behavior_paths+=("${files[@]}")
        continue
    fi
    result=$("$solver" "${files[@]}" "${settings[@]}")
    if [ "${#files[@]}" -eq 1 ]; then
        score=$(awk '{for(i=1;i<=NF;i++) if($i~/^exploitability=/){sub("exploitability=","",$i); print $i}}' <<< "$result")
        [[ -n "$score" ]]
        result=$(printf '%s\t%s\nbest=%s\t%s\n' "$score" "${files[0]}" "$score" "${files[0]}")
    fi
    if [ "$mode" = best ]; then
        best=${result##*$'\n'}
        value=${best#best=}
        score=${value%%$'\t'*}
        path=${value#*$'\t'}
        names+=("$run")
        paths+=("$path")
        printf '%s\t%s\t%s\n' "$run" "$score" "$path" | tee -a "$manifest"
    else
        while IFS=$'\t' read -r score path; do
            [ -n "$path" ] || continue
            names+=("$run:${path##*/}")
            paths+=("$path")
            printf '%s\t%s\t%s\n' "${names[${#names[@]}-1]}" "$score" "$path" \
                | tee -a "$manifest"
        done < <(printf '%s\n' "$result" \
            | awk -F '\t' '$1 ~ /^[0-9]+(\.[0-9]+)?$/ && NF == 2')
    fi
done

if [ "$mode" = jsd ]; then
    behavior="$log_dir/goofspiel/${prefix}_behavior.tsv"
    "$solver" behavior "${behavior_paths[@]}" "${settings[@]}" \
        > "$behavior"
    printf 'Behavior distributions and Jensen-Shannon distances: %s\n' "$behavior"
    exit 0
fi

count=${#names[@]}
if [ "$mode" = scan ]; then
    printf 'Scanned %d checkpoints; wrote %s\n' "$count" "$manifest"
    exit 0
fi
if [ "$mode" = all ] && [ "$count" -gt 128 ]; then
    echo "Evaluating $count checkpoints: use a small games argument for discovery;" >&2
    echo "confirm only reported cycles with a larger match budget." >&2
fi

declare -A payoff draw
for ((i = 0; i < ${#names[@]}; i++)); do
    payoff["$i,$i"]=0.500000
    mirror=$("$binary" match --headless "${settings[@]}" \
        "--base.load_model_path=${paths[i]}" \
        "--base.load_enemy_model_path=${paths[i]}" \
        "--base.eval_episodes=$games" \
        | tr '\r' '\n' | sed -n '/^CUDA_EVAL /p')
    [[ "$mirror" == *" draw="* ]]
    draw["$i,$i"]=$(awk '{for(i=1;i<=NF;i++) if($i~/^draw=/){sub("draw=","",$i); print $i}}' <<< "$mirror")
    for ((j = i + 1; j < ${#names[@]}; j++)); do
        forward=$("$binary" match --headless "${settings[@]}" \
            "--base.load_model_path=${paths[i]}" \
            "--base.load_enemy_model_path=${paths[j]}" \
            "--base.eval_episodes=$games" \
            | tr '\r' '\n' | sed -n '/^CUDA_EVAL /p')
        reverse=$("$binary" match --headless "${settings[@]}" \
            "--base.load_model_path=${paths[j]}" \
            "--base.load_enemy_model_path=${paths[i]}" \
            "--base.eval_episodes=$games" \
            | tr '\r' '\n' | sed -n '/^CUDA_EVAL /p')
        [[ "$forward" == *" draw="* && "$reverse" == *" draw="* ]]
        a=$(awk '{for(i=1;i<=NF;i++) if($i~/^score=/){sub("score=","",$i); print $i}}' <<< "$forward")
        b=$(awk '{for(i=1;i<=NF;i++) if($i~/^score=/){sub("score=","",$i); print $i}}' <<< "$reverse")
        da=$(awk '{for(i=1;i<=NF;i++) if($i~/^draw=/){sub("draw=","",$i); print $i}}' <<< "$forward")
        db=$(awk '{for(i=1;i<=NF;i++) if($i~/^draw=/){sub("draw=","",$i); print $i}}' <<< "$reverse")
        ij=$(awk -v a="$a" -v b="$b" 'BEGIN {printf "%.6f", (a + 1 - b)/2}')
        ji=$(awk -v x="$ij" 'BEGIN {printf "%.6f", 1-x}')
        d=$(awk -v a="$da" -v b="$db" 'BEGIN {printf "%.6f", (a+b)/2}')
        payoff["$i,$j"]=$ij
        payoff["$j,$i"]=$ji
        draw["$i,$j"]=$d
        draw["$j,$i"]=$d
        printf '%s vs %s: score=%s draw=%s\n' "${names[i]}" "${names[j]}" "$ij" "$d"
    done
done

{
    printf 'policy'
    printf '\t%s' "${names[@]}"
    printf '\n'
    for ((i = 0; i < ${#names[@]}; i++)); do
        printf '%s' "${names[i]}"
        for ((j = 0; j < ${#names[@]}; j++)); do
            printf '\t%s' "${payoff[$i,$j]}"
        done
        printf '\n'
    done
} | tee "$matrix"

{
    printf 'policy'
    printf '\t%s' "${names[@]}"
    printf '\n'
    for ((i = 0; i < ${#names[@]}; i++)); do
        printf '%s' "${names[i]}"
        for ((j = 0; j < ${#names[@]}; j++)); do
            printf '\t%s' "${draw[$i,$j]}"
        done
        printf '\n'
    done
} > "$draws"

printf 'a\tb\tc\tdirection\n' > "$cycles"
for ((i = 0; i < ${#names[@]}; i++)); do
    for ((j = i + 1; j < ${#names[@]}; j++)); do
        for ((k = j + 1; k < ${#names[@]}; k++)); do
            if awk -v ab="${payoff[$i,$j]}" -v bc="${payoff[$j,$k]}" \
                    -v ca="${payoff[$k,$i]}" -v t="$threshold" \
                    'BEGIN {exit !(ab>0.5+t && bc>0.5+t && ca>0.5+t)}'; then
                printf '%s\t%s\t%s\ta>b>c>a\n' \
                    "${names[i]}" "${names[j]}" "${names[k]}" | tee -a "$cycles"
            fi
            if awk -v ac="${payoff[$i,$k]}" -v cb="${payoff[$k,$j]}" \
                    -v ba="${payoff[$j,$i]}" -v t="$threshold" \
                    'BEGIN {exit !(ac>0.5+t && cb>0.5+t && ba>0.5+t)}'; then
                printf '%s\t%s\t%s\ta>c>b>a\n' \
                    "${names[i]}" "${names[j]}" "${names[k]}" | tee -a "$cycles"
            fi
        done
    done
done

printf 'Wrote %s, %s, %s, and %s\n' "$manifest" "$matrix" "$draws" "$cycles"
