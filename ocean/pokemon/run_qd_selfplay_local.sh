#!/usr/bin/env bash
# Fresh selfplay-only comparison. No pretrained league is loaded or scored.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
if [[ "${1:-}" == "--after-smoke" ]]; then
  printf 'Waiting for the selfplay smoke test to finish successfully...\n'
  # The runner holds this lock until it exits. Do not overlap GPU jobs or
  # start the long experiment if the smoke test stopped before completion.
  flock runs/pokemon_qd_selfplay_smoke_20260912/lock \
    /home/felix/puffertank/.venv/bin/python -c \
    'import json; from pathlib import Path; s=json.loads(Path("runs/pokemon_qd_selfplay_smoke_20260912/state.json").read_text()); assert s["generation"] == 1 and s["opponent_mode"] == "selfplay", "Smoke test incomplete; long run not started"; print("Selfplay smoke test complete. Starting fresh long run.")'
fi
/home/felix/puffertank/.venv/bin/python ocean/pokemon/qd.py run \
  --trainer build/pokemon/puffer_qd --opponent-mode selfplay \
  --out runs/pokemon_qd_selfplay_long_20260912 \
  --population 4 --generations 2 --train-steps 100000000 --games 64 \
  --total-agents 1024 --horizon 512 --gae-lambda 0.995 \
  --learning-rate 0.0001 --entropy-coef 0.0005 \
  --history-panel-size 4 --selfplay-checkpoint-interval 10
