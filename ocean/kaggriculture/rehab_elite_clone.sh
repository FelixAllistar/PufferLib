#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if (($# < 2 || $# > 3)); then
    printf 'Usage: %s MODEL RUN_ID [TIMESTEPS]\n' "$0" >&2
    exit 2
fi

model=$1
run_id=$2
timesteps=${3:-50000000}

[[ -f $model ]] || { printf 'Missing model: %s\n' "$model" >&2; exit 1; }
[[ $model =~ _([0-9]+)x([0-9]+)_ ]] || {
    printf 'Cannot infer architecture from model name: %s\n' "$model" >&2
    exit 1
}
hidden=${BASH_REMATCH[1]}
layers=${BASH_REMATCH[2]}

exec ./puffer train kaggriculture \
    "base.load_model_path=$model" \
    "base.run_id=$run_id" \
    base.eval_deterministic=0 \
    base.checkpoint_interval=10 \
    vec.total_agents=4096 \
    vec.num_frozen_banks=1 \
    vec.frozen_bank_pct=1 \
    "vec.frozen_bank_hidden_size=$hidden" \
    "vec.frozen_bank_num_layers=$layers" \
    "policy.hidden_size=$hidden" \
    "policy.num_layers=$layers" \
    selfplay.enabled=1 \
    selfplay.max_size=1 \
    selfplay.snapshot_interval=0 \
    selfplay.opponent_pool=None \
    selfplay.opponent_league=None \
    selfplay.opponent_pool_weights=None \
    selfplay.opponent_pool_prob=0 \
    "selfplay.magnet_path=$model" \
    env.bot_opponent_fraction=1 \
    env.bot_pass_fraction=0.5 \
    env.bot_first=0 \
    env.bot_top_fraction=0 \
    env.bot_rules_fraction=0.5 \
    env.bot_script_fraction=0 \
    env.bot_adaptive_fraction=0 \
    "train.total_timesteps=$timesteps" \
    train.learning_rate=0.001 \
    train.anneal_lr=0 \
    train.ent_coef=0.0005 \
    train.anneal_ent_coef=0 \
    train.emag_kl_coef=0.001 \
    train.emag_tau=0 \
    train.emag_cutoff=1 \
    train.minibatch_size=4096 \
    train.horizon=128 \
    train.replay_ratio=1 \
    train.epoch_sampling=1
