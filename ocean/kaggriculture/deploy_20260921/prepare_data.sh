#!/usr/bin/env bash
set -euo pipefail
cd /workspace/PufferLib
kag_deploy=ocean/kaggriculture/deploy_20260921
kag_build=ocean/kaggriculture/build/qualification_20260921
kag_dataset=data/entity_2_2_policy5_baseline_64g_v1.bc
exec > >(tee qualification/production_20260921/prepare_64g.log) 2>&1
echo 'Preparing 64 verified Majkel games: 57 train / 7 held out by episode.'
echo 'Primitive tapes preserve the costly parsing/parity work for future controllers.'
make -C ocean/kaggriculture CC=clang BUILD=build/qualification_20260921 \
    build/qualification_20260921/libbc_replay.so
uv run --no-project --python /venv/main/bin/python python \
    ocean/kaggriculture/build_entity_bc_dataset.py \
    --manifest "$kag_deploy/majkel64_manifest.json" \
    --profile ocean/kaggriculture/bc_2_2_baseline_20260921.ini \
    --lib "$kag_build/libbc_replay.so" --teacher Majkel1337 \
    --output "$kag_dataset"
echo 'DATA PREPARED: 64 games with a separate episode holdout.'
echo 'Trainer preflight runs after active-config promotion; the old config lacks new BC keys.'
