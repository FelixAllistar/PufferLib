#!/bin/bash
set -e

models=(
    checkpoints/goofspiel/validate_exact_every_epoch_seed102/0000000009961472.bin
    checkpoints/goofspiel/validate_exact_every_epoch_seed103/0000000009961472.bin
    checkpoints/goofspiel/validate_exact0_seed101/0000000009961472.bin
    checkpoints/goofspiel/validate_exact0_seed102/0000000009961472.bin
    checkpoints/goofspiel/validate_exact0_seed103/0000000009961472.bin
    checkpoints/goofspiel/sweep_1785414564503_0017/0000000009961472.bin
)
pool=$(IFS=,; echo "${models[*]}")

exec ./puffer train goofspiel \
    base.load_model_path=checkpoints/goofspiel/validate_exact_every_epoch_seed101/0000000009961472.bin \
    base.run_id=goofspiel4_robust_mixed_v1 \
    base.checkpoint_interval=1 \
    env.exact_exploiter=1 \
    env.exact_exploiter_banks=1 \
    vec.num_frozen_banks=4 \
    vec.frozen_bank_pct=0.75 \
    selfplay.opponent_pool="$pool" \
    selfplay.opponent_pool_prob=0.8 \
    selfplay.pfsp_alpha=0 \
    selfplay.opp_timeout_steps=1000000 \
    train.total_timesteps=30000000 \
    "$@"
