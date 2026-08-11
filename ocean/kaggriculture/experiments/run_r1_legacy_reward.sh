#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."

# Same data/start distribution as R0. The only material change is disabling
# terminal reward knobs that the historical CUDA path silently ignored while
# ExpL was trained.
exec ./puffer train kaggriculture \
    base.run_id=kag_dag_r1_legacy_reward \
    base.load_model_path=saved/kaggriculture_hall_of_fame/expL_sourcezero.bin \
    base.seed=811 base.async=0 \
    vec.total_agents=4096 vec.num_frozen_banks=0 vec.frozen_bank_pct=0 \
    policy.hidden_size=128 policy.num_layers=2 \
    selfplay.enabled=0 \
    env.opening_turns=0 env.reset_opening_turns=80 \
    env.reset_opening_min=10 env.reset_opening_prob=0.5 \
    env.bot_opponent_fraction=1 env.bot_top_fraction=1 \
    env.bot_rules_fraction=0 env.bot_script_fraction=0 \
    env.bot_adaptive_fraction=0 \
    env.reward_potential_scale=0.000772047148 \
    env.reward_win=0.375989795 env.reward_seed_value=0.01 \
    env.reward_product_value=0.95 env.reward_crop_value=1 \
    env.reward_animal_value=0.95 env.reward_land_value=1 \
    env.reward_productive_action=0.0002 env.reward_margin_scale=0 \
    env.reward_inactivity_threshold=0 env.reward_inactivity=0 \
    env.reward_neglect_death=0.05 \
    train.total_timesteps=40000000 train.learning_rate=0.00005 \
    train.ent_coef=0.001 train.emag_kl_coef=0 \
    train.gamma=0.999859989 train.gae_lambda=0.987793684 \
    train.replay_ratio=1 train.clip_coef=0.158850163 \
    train.vf_coef=0.02 train.vf_clip_coef=0.1 \
    train.max_grad_norm=0.750745416 train.minibatch_size=4096 \
    train.horizon=64
