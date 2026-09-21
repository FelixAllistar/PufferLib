#!/usr/bin/env bash
# Isolated production-launcher BC -> eMAG/PPO integration, never a long run.
set -euo pipefail
cd "$(dirname "$0")/../../.."
kag_binary=$(realpath "${1:?Usage: smoke_performance_binary.sh CANDIDATE_BINARY}")
if [[ -n $(nvidia-smi --query-compute-apps=pid --format=csv,noheader) ]]; then
    echo 'GPU is busy; smoke not launched.' >&2; exit 1
fi
kag_out=$(mktemp -d "$PWD/qualification/production_smoke.XXXXXX")
exec > >(tee "$kag_out/smoke.log") 2>&1
echo "Production smoke output: $kag_out"
"$kag_binary" check kaggriculture
timeout 180s "$kag_binary" train kaggriculture \
    base.run_id=bc_emag_smoke "base.checkpoint_dir=$kag_out/checkpoints" "base.log_dir=$kag_out/logs" \
    base.load_model_path=/workspace/PufferLib/qualification/production_20260921/pilot.5SAQq3/actor_100.bin \
    base.checkpoint_interval=1 base.eval_epoch_mult=0 base.perf_log=1 base.async=0 base.cudagraphs=1 \
    selfplay.eval_pool_size=0 sweep.downsample=1 \
    policy.hidden_size=512 policy.num_layers=3 vec.total_agents=256 vec.num_buffers=1 \
    vec.num_frozen_banks=8 vec.frozen_bank_pct=0.75 \
    vec.frozen_bank_hidden_size=512 vec.frozen_bank_num_layers=3 \
    env.macro_mode=2 env.macro_executor_version=2 env.reset_state_prob=0.8 \
    train.horizon=16 train.minibatch_size=64 train.total_timesteps=8192 train.gpus=1 \
    train.emag_kl_coef=0.05 train.emag_tau=0.01 train.emag_cutoff=1
uv run --no-project --python /venv/main/bin/python - "$kag_out" <<'PY'
import pathlib
import sys
import numpy as np

root = pathlib.Path(sys.argv[1])
files = sorted((root / "checkpoints").rglob("*.bin"))
assert len(files) == 2, files
for path in files:
    for weights_path in (path, pathlib.Path(str(path) + ".emag")):
        weights = np.fromfile(weights_path, dtype=np.float32)
        assert weights.size == 4025560 and np.isfinite(weights).all(), weights_path
        print("Finite H512/L3 checkpoint:", weights_path)
print("PASS: production GPU launcher loaded existing BC and completed two eMAG/PPO updates.")
PY
sha256sum "$kag_binary"
touch "$kag_out/PASS"
