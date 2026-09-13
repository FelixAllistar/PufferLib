#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
event_qd_out=runs/pokemon_event_qd_gen2_20260913
event_qd_steps=100000000
event_qd_games=64
event_qd_seed=4601
if [[ "${1:-}" == "--smoke" ]]; then
  event_qd_out=runs/pokemon_event_qd_smoke_20260913
  event_qd_steps=1048576
  event_qd_games=2
  event_qd_seed=601
elif [[ $# -gt 0 ]]; then
  echo 'Usage: bash ocean/pokemon/run_event_qd_local.sh [--smoke]' >&2
  exit 1
fi
/home/felix/puffertank/.venv/bin/python ocean/pokemon/event_qd.py \
  --parent runs/pokemon_qd_selfplay_long_20260912/candidates/g0000_control/checkpoints/pokemon/g0000_control_1789266489107356907/0000000099614720.bin \
  --panel runs/pokemon_qd_selfplay_long_20260912/generations/g0001/panel.json \
  --out "$event_qd_out" --train-steps "$event_qd_steps" --games "$event_qd_games" --generations 2 --seed "$event_qd_seed" \
  --warmstart \
    runs/pokemon_behavior_manual_20260913/seed101_control/checkpoints/pokemon/seed101_control/0000000099614720.bin \
    runs/pokemon_behavior_manual_20260913/seed101_sleep_plus/checkpoints/pokemon/seed101_sleep_plus/0000000099614720.bin \
    runs/pokemon_behavior_manual_20260913/seed101_sleep_minus/checkpoints/pokemon/seed101_sleep_minus/0000000099614720.bin \
    runs/pokemon_behavior_manual_20260913/seed101_paralysis_plus/checkpoints/pokemon/seed101_paralysis_plus/0000000099614720.bin \
    runs/pokemon_behavior_manual_20260913/seed101_paralysis_minus/checkpoints/pokemon/seed101_paralysis_minus/0000000099614720.bin \
  --holdout \
    runs/pokemon_qd_selfplay_long_20260912/candidates/g0001_c000/checkpoints/pokemon/*/0000000099614720.bin \
    runs/pokemon_qd_selfplay_long_20260912/candidates/g0001_c001/checkpoints/pokemon/*/0000000099614720.bin \
    runs/pokemon_qd_selfplay_long_20260912/candidates/g0001_c002/checkpoints/pokemon/*/0000000099614720.bin \
    runs/pokemon_qd_selfplay_long_20260912/candidates/g0001_c003/checkpoints/pokemon/*/0000000099614720.bin
