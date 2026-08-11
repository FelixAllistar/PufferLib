#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."

# Fine-tune the confirmed full-trajectory BC champion on real root games while
# a fixed conditional KL reference prevents catastrophic imitation forgetting.
exec ./puffer train kaggriculture \
    base.run_id=kag_dag_c2_frozen_bc \
    base.load_model_path=saved/kaggriculture_hall_of_fame/expR_fullbc_top_e25.bin \
    base.seed=821 base.async=0 \
    vec.total_agents=4096 vec.num_frozen_banks=0 vec.frozen_bank_pct=0 \
    policy.hidden_size=128 policy.num_layers=2 \
    selfplay.enabled=0 \
    selfplay.magnet_path=saved/kaggriculture_hall_of_fame/expR_fullbc_top_e25.bin \
    env.opening_turns=0 env.reset_opening_turns=0 \
    env.reset_opening_min=0 env.reset_opening_prob=0 \
    env.bot_opponent_fraction=1 env.bot_top_fraction=1 \
    env.bot_rules_fraction=0 env.bot_script_fraction=0 \
    env.bot_adaptive_fraction=0 \
    env.reward_potential_scale=0.000772047148 \
    env.reward_win=0.375989795 env.reward_seed_value=0.01 \
    env.reward_product_value=0.95 env.reward_crop_value=1 \
    env.reward_animal_value=0.95 env.reward_land_value=1 \
    env.reward_productive_action=0.0002 env.reward_margin_scale=0.3 \
    env.reward_inactivity_threshold=500 env.reward_inactivity=3 \
    env.reward_neglect_death=0.05 \
    train.total_timesteps=40000000 train.learning_rate=0.00001 \
    train.ent_coef=0.0005 train.emag_kl_coef=0.005 train.emag_tau=0 \
    train.gamma=0.999859989 train.gae_lambda=0.987793684 \
    train.replay_ratio=1 train.clip_coef=0.158850163 \
    train.vf_coef=0.02 train.vf_clip_coef=0.1 \
    train.max_grad_norm=0.750745416 train.minibatch_size=4096 \
    train.horizon=64
