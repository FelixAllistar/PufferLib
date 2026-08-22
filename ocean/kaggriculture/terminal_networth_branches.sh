#!/usr/bin/env bash
# Sequential validation of the retained-terminal-net-worth objective.  The
# warm branch asks whether it improves an already competent economy; the cold
# branch asks whether the same reward can discover expansion from scratch.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

warm_source=${KAG_TERMINAL_WARM_SOURCE:-saved/kaggriculture_league_1024x2_v1/run_economic_reward_diverse_coldstart_1024x2_v2_0000001999634432.bin}
warm_steps=${KAG_TERMINAL_WARM_STEPS:-500000000}
cold_steps=${KAG_TERMINAL_COLD_STEPS:-500000000}

[[ -f $warm_source ]] || {
    printf 'missing warm-start checkpoint: %s\n' "$warm_source" >&2
    exit 1
}

common=(
    base.checkpoint_interval=48
    base.eval_deterministic=0
    vec.total_agents=8192
    vec.num_frozen_banks=8
    vec.frozen_bank_pct=0.75
    vec.frozen_bank_hidden_size=1024
    vec.frozen_bank_num_layers=2
    policy.hidden_size=1024
    policy.num_layers=2
    selfplay.enabled=1
    selfplay.max_size=16
    selfplay.snapshot_interval=50000000
    selfplay.opponent_pool=None
    selfplay.opponent_league=saved/kaggriculture_league_1024x2_v1/league.ini
    selfplay.opponent_pool_weights=None
    selfplay.opponent_pool_prob=0.75
    selfplay.eval_pool_size=8
    selfplay.eval_metric=money
    selfplay.magnet_path=None
    env.reward_potential_scale=0.5
    env.reward_potential_gamma=0.99970
    env.reward_money_scale=1
    env.bot_opponent_fraction=0.25
    env.bot_pass_fraction=0
    env.bot_first=0
    env.bot_top_fraction=0
    env.bot_rules_fraction=0.25
    env.bot_script_fraction=0.3
    env.bot_adaptive_fraction=0.7
    train.learning_rate=0.00188
    train.anneal_lr=1
    train.gamma=0.99970
    train.gae_lambda=0.98745
    train.reward_clip=0
    train.ent_coef=0.000334
    train.emag_kl_coef=0
    train.minibatch_size=4096
    train.horizon=128
)

printf 'RUN terminal_networth_warm_1024x2_v1 steps=%s source=%s\n' \
    "$warm_steps" "$warm_source"
./puffer train kaggriculture "${common[@]}" \
    base.run_id=terminal_networth_warm_1024x2_v1 \
    "base.load_model_path=$warm_source" \
    "train.total_timesteps=$warm_steps"

printf 'RUN terminal_networth_cold_1024x2_v1 steps=%s source=None\n' \
    "$cold_steps"
./puffer train kaggriculture "${common[@]}" \
    base.run_id=terminal_networth_cold_1024x2_v1 \
    base.load_model_path=None \
    "train.total_timesteps=$cold_steps"

printf 'TERMINAL NET-WORTH BRANCHES COMPLETE\n'
