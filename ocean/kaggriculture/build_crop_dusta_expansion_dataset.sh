#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
factory=${KAG_CROP_DUSTA_FACTORY:-/workspace/elite_replays/clone_factory}
selection=${KAG_CROP_DUSTA_SELECTION:-$factory/crop_dusta_expand200.tsv}
dataset=${KAG_CROP_DUSTA_DATASET:-$factory/datasets/crop_dusta_expand200_1280.bc}
python_bin=${KAG_CROP_DUSTA_PYTHON:-python3}
jobs=${KAG_CROP_DUSTA_IMPORT_JOBS:-7}
parts=$factory/datasets/crop_dusta_expand200_parts

[[ -s $selection ]] || { echo "missing selection: $selection" >&2; exit 1; }
mkdir -p "$parts" "$(dirname "$dataset")"
cd "$repo_root"

mapfile -t archives < <(
    awk -F '\t' 'NR > 1 {split($2, value, ":"); print value[1]}' "$selection" \
        | sort -u
)
(( ${#archives[@]} > 0 )) || { echo "selection has no sources" >&2; exit 1; }

running=0
for archive in "${archives[@]}"; do
    stem=$(basename "$archive" .zip)
    part=$parts/$stem.bc
    if [[ -s $part ]]; then
        continue
    fi
    "$python_bin" ocean/kaggriculture/import_elite_replays.py \
        "$archive" \
        --output "$part" \
        --manifest "$part.players.tsv" \
        --audit-json "$part.audit.json" \
        --agent "Crop Dusta" \
        --trajectory-file "$selection" \
        --exact-version 1.32.7 \
        --players both \
        --min-final-money 60000 \
        >"$part.log" 2>&1 &
    ((running += 1))
    if (( running >= jobs )); then
        wait -n
        ((running -= 1))
    fi
done
wait

part_datasets=()
for archive in "${archives[@]}"; do
    stem=$(basename "$archive" .zip)
    part_datasets+=("$parts/$stem.bc")
done
"$python_bin" ocean/kaggriculture/merge_bc_datasets.py \
    "$dataset" "${part_datasets[@]}"

{
    first=1
    for part in "${part_datasets[@]}"; do
        if (( first )); then
            cat "$part.players.tsv"
            first=0
        else
            tail -n +2 "$part.players.tsv"
        fi
    done
} >"$dataset.players.tsv"

printf 'Crop Dusta expansion dataset: %s\n' "$dataset"
