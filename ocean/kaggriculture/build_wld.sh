#!/usr/bin/env bash
# Experimental training binary; never overwrites the normal ./puffer binary.
set -euo pipefail
cd "$(dirname "$0")/../.."
export NVCC_EXTRA="${NVCC_EXTRA:-} -DKAG_WITH_PAIRED_CRITIC"
exec bash build.sh kaggriculture puffer_wld "$@"
