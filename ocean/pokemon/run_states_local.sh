#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
exec /home/felix/puffertank/.venv/bin/python ocean/pokemon/state_experiment.py run \
  --out runs/pokemon_states_20260913
