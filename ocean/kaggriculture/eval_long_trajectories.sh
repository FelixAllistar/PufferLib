#!/usr/bin/env bash
# Evaluate evenly spaced checkpoints from each 1B continuation.  Each run is
# ranked against its own trajectory and against fixed pass/rules opponents.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

stamp=${KAG_TRAJ_STAMP:-$(date +%Y%m%d%H%M%S)}
games=${KAG_TRAJ_GAMES:-50}
jobs=${KAG_TRAJ_JOBS:-4}
gpu_agents=${KAG_TRAJ_GPU_AGENTS:-64}
profile=${KAG_TRAJ_PROFILE:-0}
profile_games=${KAG_TRAJ_PROFILE_GAMES:-20}
profile_jobs=${KAG_TRAJ_PROFILE_JOBS:-2}
profile_agents=${KAG_TRAJ_PROFILE_GPU_AGENTS:-64}
profile_count=${KAG_TRAJ_PROFILE_COUNT:-12}
queue_log="logs/kaggriculture/long_trajectory_${stamp}.log"
mkdir -p logs/kaggriculture

if ! [[ $profile_count =~ ^[0-9]+$ ]] || ((profile_count < 2)); then
    printf 'KAG_TRAJ_PROFILE_COUNT must be an integer >= 2\n' >&2
    exit 2
fi
profile_tmp=$(mktemp -d)
trap 'rm -rf "$profile_tmp"' EXIT

runs=(
  "run34_selfplay|checkpoints/kaggriculture/kag_long_run34_selfplay_20260817233015"
  "run34_mixedbots|checkpoints/kaggriculture/kag_long_run34_mixedbots_20260817233015"
  "run34_passonly|checkpoints/kaggriculture/kag_long_run34_passonly_20260817233015"
  "run40_selfplay|checkpoints/kaggriculture/kag_long_run40_selfplay_20260817233015"
  "run40_mixedbots|checkpoints/kaggriculture/kag_long_run40_mixedbots_20260817233015"
  "run40_passonly|checkpoints/kaggriculture/kag_long_run40_passonly_20260817233015"
  "run44_selfplay|checkpoints/kaggriculture/kag_long_run44_selfplay_20260817233015"
  "run44_mixedbots|checkpoints/kaggriculture/kag_long_run44_mixedbots_20260817233015"
  "run44_passonly|checkpoints/kaggriculture/kag_long_run44_passonly_20260817233015"
)

printf 'Kaggriculture trajectory evaluation: %d runs, range 0:100:12, %s games/pair\n' \
    "${#runs[@]}" "$games" | tee "$queue_log"
if [[ $profile == 1 ]]; then
    printf 'Behavior profiles enabled: %s games/checkpoint, %s sampled checkpoints/run\n' \
        "$profile_games" "$profile_count" | tee -a "$queue_log"
fi
for row in "${runs[@]}"; do
    IFS='|' read -r name directory <<< "$row"
    [[ -d $directory ]] || { printf 'Missing run directory: %s\n' "$directory" | tee -a "$queue_log"; exit 1; }
    output="logs/kaggriculture/long_${name}_${stamp}"
    printf '\n=== START %s ===\n' "$name" | tee -a "$queue_log"
    ./ocean/kaggriculture/eval_population.sh \
        --games "$games" --jobs "$jobs" --gpu-agents "$gpu_agents" \
        --fixed pass,rules --range 0:100:12 --output "$output" "$directory" \
        2>&1 | tee -a "$queue_log"
    if [[ $profile == 1 ]]; then
        # profile_population_gpu.sh records the detailed behavior counters, but
        # intentionally scans named checkpoints.  Materialize the same evenly
        # spaced numeric checkpoints used by the matrix as hard links so this
        # optional pass stays bounded and does not duplicate model storage.
        profile_input="$profile_tmp/$name"
        mkdir -p "$profile_input"
        mapfile -t profile_files < <(find "$directory" -maxdepth 1 -type f \
            -regextype posix-extended -regex '.*/[0-9]{16}\\.bin' -print | sort)
        profile_total=${#profile_files[@]}
        if ((profile_total < profile_count)); then
            printf 'Skipping behavior profile for %s: only %d checkpoints\n' \
                "$name" "$profile_total" | tee -a "$queue_log"
        else
            for ((profile_pick=0; profile_pick<profile_count; profile_pick++)); do
                profile_idx=$((profile_pick * (profile_total - 1) / (profile_count - 1)))
                profile_src=${profile_files[profile_idx]}
                profile_base=${profile_src##*/}
                ln "$profile_src" "$profile_input/${name}_${profile_base}" 2>/dev/null \
                    || cp "$profile_src" "$profile_input/${name}_${profile_base}"
            done
            ./ocean/kaggriculture/profile_population_gpu.sh \
                --games "$profile_games" --jobs "$profile_jobs" \
                --gpu-agents "$profile_agents" --output "${output}_profile" \
                "$profile_input" 2>&1 | tee -a "$queue_log"
        fi
    fi
    printf '=== DONE %s ranking=%s_ranking.tsv fixed=%s_fixed.tsv ===\n' \
        "$name" "$output" "$output" | tee -a "$queue_log"
done
printf '\n=== LONG TRAJECTORY EVALUATION COMPLETE ===\n' | tee -a "$queue_log"
