#!/usr/bin/env bash
set -euo pipefail

# Build a 1024x2 clone from behavior-stable Crop Dusta games that reach three
# plots by turn 200.  The first 200 recurrent rows are emphasized because a
# small whole-season cross-entropy improvement was previously insufficient to
# make the deterministic policy leave PASS at the root.

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
factory=${KAG_CROP_DUSTA_FACTORY:-/workspace/elite_replays/clone_factory}
replays=${KAG_CROP_DUSTA_REPLAYS:-/workspace/elite_replays/raw/kaggriculture-episodes-*/kaggriculture-episodes-*.zip}
selection=${KAG_CROP_DUSTA_SELECTION:-$factory/crop_dusta_expand200.tsv}
dataset=${KAG_CROP_DUSTA_DATASET:-$factory/datasets/crop_dusta_expand200_1280.bc}
model=${KAG_CROP_DUSTA_MODEL:-saved/kaggriculture_crop_dusta_expand200_1280_1024x2_e50.bin}
epochs=${KAG_CROP_DUSTA_EPOCHS:-50}

mkdir -p "$factory/datasets" "$(dirname "$repo_root/$model")"
cd "$repo_root"

if [[ ! -s $selection ]]; then
    python ocean/kaggriculture/select_agent_expansion_replays.py \
        "$replays" \
        --agent "Crop Dusta" \
        --exact-version 1.32.7 \
        --minimum-final-money 60000 \
        --land-turn 200 \
        --minimum-land 3 \
        --output "$selection"
fi

if [[ ! -s $dataset ]]; then
    python ocean/kaggriculture/import_elite_replays.py \
        "$replays" \
        --output "$dataset" \
        --manifest "$dataset.players.tsv" \
        --audit-json "$dataset.audit.json" \
        --agent "Crop Dusta" \
        --trajectory-file "$selection" \
        --exact-version 1.32.7 \
        --players both \
        --min-final-money 60000
fi

KAG_ELITE_BC_DATA="$dataset" \
KAG_ELITE_BC_OUTPUT="$model" \
KAG_ELITE_BC_HIDDEN=1024 \
KAG_ELITE_BC_LAYERS=2 \
KAG_ELITE_BC_OPENING_STEPS=200 \
KAG_ELITE_BC_OPENING_WEIGHT=4 \
KAG_ELITE_BC_ROOT_WEIGHT=16 \
KAG_ELITE_BC_ARGMAX_MARGIN=0.5 \
KAG_ELITE_BC_OPENING_ARGMAX_COEF=0.25 \
KAG_ELITE_BC_DETAILED_STATS=1 \
KAG_ELITE_BC_VALIDATION_GAMES=64 \
KAG_ELITE_BC_REPORT_INTERVAL=5 \
    ./ocean/kaggriculture/train_elite_bc.sh "$epochs"

printf 'Crop Dusta expansion clone: %s\n' "$model"
