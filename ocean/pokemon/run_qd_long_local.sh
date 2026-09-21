#!/usr/bin/env bash
# Fresh long comparison: 11 jobs x 100M requested steps, not a resumed old run.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
/home/felix/puffertank/.venv/bin/python ocean/pokemon/qd.py run \
  --trainer build/pokemon/puffer_qd \
  --native-league leagues/pokemon/named_roster/native_two_20260910.ini \
  --out runs/pokemon_qd_fresh_long_20260912 \
  --population 4 --generations 2 --train-steps 100000000 --games 64 \
  --total-agents 1024 --horizon 512 --gae-lambda 0.995 \
  --learning-rate 0.0001 --entropy-coef 0.0005
