#!/usr/bin/env bash
set -euo pipefail
experiment=/workspace/PufferLib/reports/pokemon_experiments/20261018_capacity1024
cd "$experiment"
test -f config/pokemon.ini
test ! -e capacity.history
test ! -e checkpoints
printf '%s  %s\n' 07849fd021b6534648cdbcddf98375ae855a9e31c12b5f9829c607d0a2f93a78 /workspace/PufferLib/puffer | sha256sum -c -
exec /workspace/PufferLib/puffer sweep \
    --base.checkpoint_dir="$experiment/checkpoints" \
    --base.log_dir="$experiment/logs" \
    --base.load_model_path=None --train.total_timesteps=1000000000 \
    --sweep.max_runs=500 --sweep.max_failures=32
