#!/usr/bin/env bash
# Run on Vast from /workspace/PufferLib. Separate from ordinary train defaults.
set -euo pipefail
cd /workspace/PufferLib
experiment=/workspace/PufferLib/reports/pokemon_experiments/20261018_continue_r26_500_v2
checkpoint=/workspace/PufferLib/leagues/pokemon/rr50m_bf16_20261017/members/master/r000026-a001/weights.bin
opponents=/workspace/PufferLib/leagues/pokemon/rr50m_bf16_20261017/rounds/r000026/master-opponents.ini
test ! -e "$experiment/continuation.history"
test ! -e "$experiment/checkpoints"
if ! test -d "$experiment"; then
mkdir -p "$experiment"
mkdir -p "$experiment/config"
cp config/default.ini "$experiment/config/default.ini"
cp reports/pokemon_experiments/20261017_roundrobin_50m/continuation_sweep.ini "$experiment/config/pokemon.ini"
# Isolate config while resolving existing engine, evaluator and data paths.
for directory in ocean build saved runs leagues reports resources; do
    if test -e "/workspace/PufferLib/$directory"; then
        ln -s "/workspace/PufferLib/$directory" "$experiment/$directory"
    fi
done
cp config/pokemon.ini "$experiment/defaults_at_launch.ini"
cp config/default.ini "$experiment/generic_defaults_at_launch.ini"
cp "$opponents" "$experiment/opponents.ini"
cp "$opponents.opponents" "$experiment/opponents.txt"
fi
printf '%s  %s\n' 440b4dd80bebcc01fc491fdd6b1b9bed046953936e7d597c2207f2b0a6335b40 "$checkpoint" | sha256sum -c -
printf '%s  %s\n' 07849fd021b6534648cdbcddf98375ae855a9e31c12b5f9829c607d0a2f93a78 puffer | sha256sum -c -
cd "$experiment"
exec /workspace/PufferLib/puffer sweep --base.load_model_path="$checkpoint" \
    --base.checkpoint_dir="$experiment/checkpoints" \
    --base.log_dir="$experiment/logs" \
    --vec.num_buffers=1 --vec.num_policies=14 \
    --selfplay.initial_opponents="$opponents.opponents" \
    --env.native_league="$opponents" \
    --train.total_timesteps=100663296 --train.anneal_lr=1 \
    --sweep.train.total_timesteps.min=50000000 \
    --sweep.train.total_timesteps.max=500000000 \
    --sweep.train.learning_rate.min=0.00001 \
    --sweep.max_runs=500 --sweep.max_failures=32 \
    --sweep.history_path="$experiment/continuation.history" \
    --oak.enabled=1 --oak.games=256 --oak.budget=4096 --oak.workers=6 \
    --oak.seed=20261015
