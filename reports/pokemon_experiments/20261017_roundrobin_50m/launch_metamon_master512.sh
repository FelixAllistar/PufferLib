#!/usr/bin/env bash
set -euo pipefail
cd /workspace/PufferLib
export METAMON_CACHE_DIR=/workspace/PufferLib/build/pokemon/metamon-cache
export TORCH_COMPILE_DISABLE=1
exec build/pokemon/metamon-eval-env-cu128/bin/python -u \
    ocean/pokemon/benchmarks/evaluate_private.py \
    reports/pokemon_experiments/20261018_continue_r26_500_v2/checkpoints/pokemon/sweep_1792341398037_0050/0000000089653248.bin \
    --opponent TaurosV0 --mode argmax --device cuda --threads 1 \
    --games 32 --seed 20261020 \
    --output leagues/pokemon/rr50m_bf16_20261017/evaluations/metamon-best-teams-32/master-sweep0050
