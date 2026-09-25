#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

count=${1:-16}
first_seed=${2:-1001}
prefix=${3:-population64x1}
shift "$(( $# < 3 ? $# : 3 ))"

binary=${GOOFSPIEL_TRAIN_BINARY:-./puffer_goofspiel}
checkpoint_dir=${GOOFSPIEL_CHECKPOINT_DIR:-checkpoints}
[[ "$count" =~ ^[1-9][0-9]*$ && "$first_seed" =~ ^[0-9]+$ ]]
[[ "$prefix" =~ ^[a-zA-Z0-9_-]+$ ]]
[[ -x "$binary" ]]
for arg in "$@"; do
    case "$arg" in
        --base.run_id=*|--base.seed=*|--env.seed=*|--base.checkpoint_dir=*|--base.load_model_path=*)
            echo "Population-owned setting: $arg" >&2
            exit 2 ;;
    esac
done

# Check the entire population before starting any expensive run.
for ((i = 0; i < count; i++)); do
    seed=$((first_seed + i))
    run_id="${prefix}_seed${seed}"
    if [ -e "$checkpoint_dir/goofspiel/$run_id" ]; then
        echo "Run already exists: $run_id" >&2
        exit 1
    fi
done

for ((i = 0; i < count; i++)); do
    seed=$((first_seed + i))
    run_id="${prefix}_seed${seed}"
    printf 'Population %d/%d: %s\n' "$((i + 1))" "$count" "$run_id"
    "$binary" train \
        "--base.seed=$seed" "--env.seed=$seed" "--base.run_id=$run_id" \
        "--base.checkpoint_dir=$checkpoint_dir" --base.load_model_path=None \
        --base.checkpoint_interval=20 --base.eval_episodes=0 \
        --policy.hidden_size=64 --policy.num_layers=1 \
        --selfplay.enabled=1 --selfplay.initial_opponents=None \
        --env.exact_exploiter=1 --env.exact_exploiter_banks=1 \
        --env.exact_exploiter_history=256 --env.exact_exploiter_current_prob=0.5 \
        --train.total_timesteps=8000000 \
        --train.learning_rate=0.00826629158 --train.gamma=0.989783943 \
        --train.gae_lambda=0.88410759 --train.replay_ratio=2.40982485 \
        --train.clip_coef=0.02 --train.vf_coef=1.18337274 \
        --train.vf_clip_coef=4.99999952 --train.max_grad_norm=0.702080846 \
        --train.ent_coef=0.0318561755 --train.anneal_ent_coef=0 \
        --train.momentum=0.926009178 "$@"
done

printf 'Population training complete: %s/goofspiel/%s_seed*\n' "$checkpoint_dir" "$prefix"
