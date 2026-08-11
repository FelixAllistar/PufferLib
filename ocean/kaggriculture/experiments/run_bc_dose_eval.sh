#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$root"

games=${KAG_EVAL_GAMES:-200}
./ocean/kaggriculture/eval_population.sh \
    --games "$games" --jobs 1 --gpu-agents "$games" --fixed top \
    --output logs/kaggriculture/bc_dose_screen \
    saved/kaggriculture_hall_of_fame/expL_sourcezero.bin \
    saved/kaggriculture_hall_of_fame/expR_fullbc_top_e25.bin \
    saved/kaggriculture_bc_dose/full_top_e15.bin \
    saved/kaggriculture_bc_dose/full_top_e20.bin \
    saved/kaggriculture_bc_dose/full_top_e30.bin \
    saved/kaggriculture_bc_dose/full_top_e35.bin
