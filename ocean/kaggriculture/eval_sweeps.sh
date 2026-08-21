#!/bin/bash
# Cross-evaluate checkpoint samples from many sweep runs against the active
# league. This wraps eval_population.sh (which already does the O(N^2) seat-
# balanced matrix, fixed-opponent checks, cycles, and a hash-keyed cache) and
# adds a margin ranking that is not exposed by the sweep metric.
#
# Usage:
#   eval_sweeps.sh [--sample N] [--games N] [--jobs N] [SWEEP_DIR ...]
#
# With no SWEEP_DIRs, every checkpoints/kaggriculture/sweep_* run is processed.
# Each run is sampled to N checkpoints plus the league, so one run stays well
# under eval_population.sh's 32-policy cap while still carrying the league as
# a fixed reference in every matrix.
set -euo pipefail

cd "$(dirname "$0")/../.."

kag_sample=6
kag_games=50
kag_jobs=4
kag_gpu_agents=${KAG_GPU_AGENTS:-64}
kag_fixed="pass,rules,top"
kag_league="saved/kaggriculture_league_v6"
kag_cache="logs/kaggriculture/sweeps_payoffs.tsv"
kag_out="logs/kaggriculture/sweeps_eval"
kag_inputs=()

kag_usage() {
    printf '%s\n' \
        "Usage: $0 [options] [SWEEP_DIR ...]" \
        "" \
        "  --sample N       Checkpoints sampled per sweep run (default 6)" \
        "  --games N        Games per pairing, split across seats (default 50)" \
        "  --jobs N         Parallel pairings (default 4)" \
        "  --gpu-agents N   CUDA agents per matcher (default 64)" \
        "  --fixed LIST     Fixed sides (default pass,rules,top)" \
        "  --league DIR     Reference league (default saved/kaggriculture_league_v6)" \
        "  --cache FILE     Persistent payoff cache" \
        "  --output PREFIX  Output prefix (default logs/kaggriculture/sweeps_eval)" \
        "  -h, --help       Show this help"
}

while (($#)); do
    case "$1" in
        --sample) kag_sample=$2; shift 2 ;;
        --games) kag_games=$2; shift 2 ;;
        --jobs) kag_jobs=$2; shift 2 ;;
        --gpu-agents) kag_gpu_agents=$2; shift 2 ;;
        --fixed) kag_fixed=$2; shift 2 ;;
        --league) kag_league=$2; shift 2 ;;
        --cache) kag_cache=$2; shift 2 ;;
        --output) kag_out=$2; shift 2 ;;
        -h|--help) kag_usage; exit 0 ;;
        --*) printf 'Unknown option: %s\n' "$1" >&2; kag_usage >&2; exit 2 ;;
        *) kag_inputs+=("$1"); shift ;;
    esac
done

if (( ${#kag_inputs[@]} == 0 )); then
    mapfile -t kag_inputs < <(find checkpoints/kaggriculture -mindepth 1 \
        -maxdepth 1 -type d -name 'sweep_*' -print | sort)
fi
if (( ${#kag_inputs[@]} == 0 )); then
    printf 'No sweep directories found\n' >&2
    exit 1
fi

mkdir -p logs/kaggriculture
kag_master="${kag_out}.ranking.tsv"
: > "$kag_master"
printf 'rank\tpolicy\tmargin\tmean_score\tmean_money\n' > "${kag_master}.tmp"

kag_margin_from_matches() {
    # matches.tsv: a  b  score_a  draw  money_a  money_b. Each unordered pair
    # appears once; a policy's terminal money gap against the other side is
    # money_a - money_b when it is column a, else money_b - money_a.
    awk -F '\t' '
        NR == 1 {next}
        {
            a=$1; b=$2; ma=$5; mb=$6
            sum[a] += ma - mb; cnt[a]++
            sum[b] += mb - ma; cnt[b]++
        }
        END {
            for (p in sum) if (cnt[p] > 0) print p, sum[p] / cnt[p]
        }
    ' "$1"
}

for kag_sweep in "${kag_inputs[@]}"; do
    [[ -d $kag_sweep ]] || { printf 'Not a directory: %s\n' "$kag_sweep" >&2; continue; }
    kag_run=${kag_sweep##*/}
    kag_prefix="${kag_out}_${kag_run}"
    ./ocean/kaggriculture/eval_population.sh \
        --games "$kag_games" \
        --jobs "$kag_jobs" \
        --gpu-agents "$kag_gpu_agents" \
        --fixed "$kag_fixed" \
        --cache "$kag_cache" \
        --sample-run "$kag_sample" \
        --output "$kag_prefix" \
        "$kag_sweep" "$kag_league"

    kag_matches="${kag_prefix}_matches.tsv"
    [[ -f $kag_matches ]] || { printf 'Missing matches for %s\n' "$kag_run" >&2; continue; }
    declare -A kag_margin=() kag_score=() kag_money=()
    while IFS=$'\t' read -r _ policy score money; do
        [[ $policy == policy ]] && continue
        kag_score["$policy"]=$score
        kag_money["$policy"]=$money
    done < "${kag_prefix}_ranking.tsv"
    while IFS=' ' read -r policy margin; do
        [[ -n $policy ]] || continue
        kag_margin["$policy"]=$margin
    done < <(kag_margin_from_matches "$kag_matches")
    for policy in "${!kag_margin[@]}"; do
        printf '%s\t%s\t%s\t%s\t%s\n' "$policy" \
            "${kag_margin[$policy]}" \
            "${kag_score[$policy]:-0}" \
            "${kag_money[$policy]:-0}" \
            "$kag_run" >> "${kag_master}.tmp"
    done
    unset kag_margin kag_score kag_money
done

sort -t$'\t' -k2,2gr "${kag_master}.tmp" \
    | awk -F '\t' 'BEGIN{OFS="\t"} {print NR,$1,$2,$3,$4,$5}' \
    > "$kag_master"
rm -f "${kag_master}.tmp"
printf 'Wrote %s\n' "$kag_master"
if command -v column >/dev/null 2>&1; then
    column -t -s $'\t' "$kag_master" | head -40
else
    head -40 "$kag_master"
fi
