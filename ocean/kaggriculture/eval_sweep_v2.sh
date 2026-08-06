#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

if (($# < 1 || $# > 3)); then
    printf 'Usage: %s SWEEP_REPORT.tsv [TOP=8] [GAMES=50]\n' "$0" >&2
    exit 2
fi

report=$1
top=${2:-8}
games=${3:-50}
[[ -f $report ]] || { printf 'Report not found: %s\n' "$report" >&2; exit 1; }
[[ $top =~ ^[0-9]+$ && $top -ge 2 && $top -le 16 ]] || exit 2
[[ $games =~ ^[0-9]+$ && $games -ge 2 && $((games % 2)) -eq 0 ]] || exit 2

mapfile -t candidates < <(awk -F '\t' -v limit="$top" \
    'NR > 1 && $6 != "" && count < limit {print $6; count++}' "$report")
if ((${#candidates[@]} < 2)); then
    printf 'Report has fewer than two candidate checkpoints: %s\n' "$report" >&2
    exit 1
fi

baselines=(
    saved/kaggriculture_v2/champion_crop_real_opening.bin
    saved/kaggriculture_v2/real_opening_50m.bin
    saved/kaggriculture_v2/phase3_long_season.bin
)
output=${report%.tsv}_confirm

args=(--games "$games" --jobs 4 --fixed rules --output "$output")
args+=("${candidates[@]}")
for baseline in "${baselines[@]}"; do
    [[ -f $baseline ]] && args+=("$baseline")
done

exec ./ocean/kaggriculture/eval_population.sh "${args[@]}"
