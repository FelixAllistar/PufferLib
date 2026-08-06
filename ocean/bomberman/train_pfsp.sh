#!/bin/bash
set -eu

# Controlled PFSP ablation from the robust-pool parent.
run_id=${1:-pfsp_pool_v2}
parent=checkpoints/bomberman/robust_pool_v1/0000000299958272.bin
opponents=(
    saved/bomberman5/model.bin
    checkpoints/bomberman/rusher_clean_v1/0000000149946368.bin
    checkpoints/bomberman/exploit_trapper_v1/0000000149946368.bin
    checkpoints/bomberman/exploit_exploit_v1/0000000149946368.bin
)

if [ -e "checkpoints/bomberman/$run_id" ]; then
    echo "Error: run already exists: checkpoints/bomberman/$run_id" >&2
    exit 1
fi

for model in "$parent" "${opponents[@]}"; do
    if [ ! -f "$model" ]; then
        echo "Error: missing model: $model" >&2
        exit 1
    fi
done

opponent_pool=$(IFS=,; echo "${opponents[*]}")
args=(
    train bomberman
    "base.load_model_path=$parent"
    "base.run_id=$run_id"
    env.reverse_curriculum=0
    env.max_ticks=400
    env.reward_soft=0.05
    env.reward_pickup=0.10
    env.reward_alive=-0.002
    train.total_timesteps=300000000
    train.ent_coef=0.005
    selfplay.enabled=1
    selfplay.max_size=32
    "selfplay.opponent_pool=$opponent_pool"
    selfplay.opponent_pool_prob=0.5
    selfplay.pfsp_alpha=1
    selfplay.payoff_ema=0.1
    selfplay.opp_timeout_steps=5000000
    vec.num_frozen_banks=4
    vec.frozen_bank_pct=0.75
)

printf 'Starting:'
printf ' %q' ./puffer "${args[@]}"
printf '\n'
exec ./puffer "${args[@]}"
