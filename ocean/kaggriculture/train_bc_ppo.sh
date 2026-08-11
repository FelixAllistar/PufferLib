#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# < 1 || $# > 2)); then
    echo "Usage: $0 BC_CHECKPOINT [RUN_ID]" >&2
    exit 2
fi
kag_anchor=$1
kag_run=${2:-kag_bc_frozen_kl}
[[ -f $kag_anchor ]] || { echo "Checkpoint not found: $kag_anchor" >&2; exit 1; }

# Frozen-BC regularization: tau=0 keeps q fixed while PPO learns p. The bot
# half is itself mixed (35% top hybrid, 15% public/rules); the other half uses
# the compact learned league. No strategic action masks or reset curriculum.
exec ./puffer train kaggriculture \
    "base.run_id=$kag_run" "base.load_model_path=$kag_anchor" \
    base.async=0 policy.hidden_size=128 policy.num_layers=2 \
    selfplay.enabled=1 selfplay.snapshot_interval=0 \
    selfplay.opponent_league=saved/kaggriculture_league_v6/league.ini \
    selfplay.opponent_pool_prob=0.75 selfplay.pfsp_uniform_mix=0.25 \
    "selfplay.magnet_path=$kag_anchor" \
    vec.num_frozen_banks=4 vec.frozen_bank_pct=0.50 \
    env.bot_opponent_fraction=0.50 env.bot_top_fraction=0.70 \
    env.bot_rules_fraction=0.50 env.bot_script_fraction=0.40 \
    env.bot_adaptive_fraction=0.60 env.opening_turns=0 \
    env.reset_opening_turns=0 env.reset_opening_prob=0 \
    env.reward_productive_action=0 env.reward_inactivity=0 \
    env.reward_neglect_death=0 train.emag_tau=0 \
    train.emag_kl_coef=0.01 train.emag_cutoff=0.134 \
    train.learning_rate=0.0003 \
    train.anneal_lr=1 train.min_lr_ratio=0.1 train.ent_coef=0.001 \
    train.total_timesteps=200000000
