#!/usr/bin/env bash
set -euo pipefail

root="${KAG_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
run_id="${1:-kag_tagged_bridge_512x2_v1_1b}"
steps="${2:-1000000000}"

cd "$root"
if [ -d "checkpoints/kaggriculture/$run_id" ]; then
    echo "Refusing to overwrite existing run: $run_id" >&2
    exit 1
fi

./puffer train kaggriculture \
    base.run_id="$run_id" \
    base.load_model_path=None \
    selfplay.magnet_path=None \
    selfplay.enabled=1 \
    selfplay.max_size=2 \
    selfplay.opponent_pool=None \
    selfplay.opponent_league=None \
    selfplay.opponent_pool_prob=0 \
    env.episode_steps=720 \
    env.policy_market_slots=10 \
    env.policy_max_hands=240 \
    env.reset_state_prob=0 \
    env.reset_opening_prob=0 \
    env.curriculum_enabled=1 \
    env.curriculum_window=64 \
    env.curriculum_success_rate=0.60 \
    env.curriculum_rehearsal_prob=0.50 \
    env.curriculum_root_prob=0.05 \
    env.curriculum_reward=1.0 \
    env.reward_potential_scale=0 \
    env.reward_cash_scale=0 \
    env.reward_money_scale=1 \
    env.reward_win=0 \
    env.reward_progress_scale=0 \
    env.reward_progress_terminal_money_scale=0 \
    env.reward_progress_win_scale=0 \
    env.reward_progress_liquidation_days=0 \
    env.reward_progress_seed_scale=0 \
    env.reward_progress_crop_scale=0 \
    env.reward_progress_animal_scale=0 \
    env.reward_progress_product_scale=0 \
    env.reward_progress_land_scale=0 \
    env.reward_progress_maintenance_scale=0 \
    env.reward_inactivity=0 \
    env.reward_neglect_death=0 \
    env.reward_expansion_scale=0 \
    policy.hidden_size=512 \
    policy.num_layers=2 \
    vec.total_agents=8192 \
    vec.num_buffers=1 \
    vec.num_frozen_banks=8 \
    vec.frozen_bank_pct=0.75 \
    vec.frozen_bank_hidden_size=512 \
    vec.frozen_bank_num_layers=2 \
    train.learning_rate=0.00015 \
    train.anneal_lr=0 \
    train.gamma=0.99970 \
    train.gae_lambda=0.999 \
    train.replay_ratio=1 \
    train.ent_coef=0.0006 \
    train.emag_kl_coef=0 \
    train.emag_tau=0 \
    train.emag_cutoff=1 \
    train.clip_coef=0.158850163 \
    train.vf_coef=0.249999985 \
    train.vf_clip_coef=0.31161955 \
    train.max_grad_norm=0.750745416 \
    train.momentum=0.9 \
    train.reward_clip=0 \
    train.anneal_ent_coef=0 \
    train.min_ent_coef_ratio=0.1 \
    train.minibatch_size=4096 \
    train.horizon=128 \
    train.total_timesteps="$steps"
