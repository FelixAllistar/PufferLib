#!/usr/bin/env bash
set -euo pipefail
cd /workspace/PufferLib
# Sequential invocation follows the short master evaluation in the same pane.
exec build/pokemon/metamon-eval-env-cu128/bin/python -u \
    ocean/pokemon/benchmarks/search_metamon.py \
    --league leagues/pokemon/rr50m_bf16_20261017 \
    --output leagues/pokemon/rr50m_bf16_20261017/evaluations/metamon-policy-search \
    --screen-games 8 --shortlist-games 16 --final-games 32
