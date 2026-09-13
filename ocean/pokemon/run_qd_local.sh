#!/usr/bin/env bash
# Fresh initialization: deliberately no --seed-model, regardless of config latest.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
PYTHON_QD=/home/felix/puffertank/.venv/bin/python
common=(--trainer build/pokemon/puffer_qd
  --native-league leagues/pokemon/named_roster/native_two_20260910.ini
  --population 4 --total-agents 1024)
"$PYTHON_QD" ocean/pokemon/qd.py run "${common[@]}" \
  --out runs/pokemon_qd_fresh_smoke_20260912 \
  --generations 1 --train-steps 4194304 --games 8
"$PYTHON_QD" ocean/pokemon/qd.py run "${common[@]}" \
  --out runs/pokemon_qd_fresh_comparison_20260912 \
  --generations 2 --train-steps 10000000 --games 64
