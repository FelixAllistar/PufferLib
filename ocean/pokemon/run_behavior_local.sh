#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
/home/felix/puffertank/.venv/bin/python ocean/pokemon/behavior_experiment.py \
  --parent runs/pokemon_qd_selfplay_long_20260912/candidates/g0000_control/checkpoints/pokemon/g0000_control_1789266489107356907/0000000099614720.bin \
  --panel runs/pokemon_qd_selfplay_long_20260912/generations/g0001/panel.json \
  --out runs/pokemon_behavior_manual_20260913 \
  --train-steps 100000000 --games 64 --seeds 101 102 103
