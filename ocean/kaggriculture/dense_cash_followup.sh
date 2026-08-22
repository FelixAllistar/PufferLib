#!/usr/bin/env bash
# Compare two discount-consistent dense-cash weights from one frozen elite
# checkpoint. All optimizer, architecture, opponent, and net-worth settings are
# pinned to the successful elite continuation so cash weight is the only
# experimental variable.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

steps=${KAG_DENSE_CASH_STEPS:-200000000}
stamp=${KAG_DENSE_CASH_STAMP:-$(date +%Y%m%d%H%M%S)}
source_checkpoint=${KAG_DENSE_CASH_SOURCE:-saved/kaggriculture_dense_cash_sources/rerun3_499m.bin}
expected_bytes=34746368
queue_log="logs/kaggriculture/dense_cash_followup_${stamp}.log"
manifest="logs/kaggriculture/dense_cash_followup_${stamp}.tsv"

[[ -f $source_checkpoint ]] || {
    printf 'Missing source checkpoint: %s\n' "$source_checkpoint" >&2
    exit 1
}
actual_bytes=$(stat -c %s "$source_checkpoint")
[[ $actual_bytes == "$expected_bytes" ]] || {
    printf 'Checkpoint architecture mismatch: %s bytes (expected %s for 1024x2)\n' \
        "$actual_bytes" "$expected_bytes" >&2
    exit 1
}

mkdir -p logs/kaggriculture
printf 'variant\trun_id\tsource\tcash_scale\tsteps\tfinal_checkpoint\n' > "$manifest"

common=(
    "base.load_model_path=$source_checkpoint"
    base.checkpoint_interval=48
    base.eval_deterministic=0
    vec.total_agents=8192
    vec.num_frozen_banks=4
    vec.frozen_bank_pct=0.75
    vec.frozen_bank_hidden_size=1024
    vec.frozen_bank_num_layers=2
    policy.hidden_size=1024
    policy.num_layers=2
    selfplay.enabled=1
    selfplay.max_size=16
    selfplay.snapshot_interval=50000000
    selfplay.opponent_pool=None
    selfplay.opponent_league=None
    selfplay.opponent_pool_weights=None
    selfplay.opponent_pool_prob=0
    selfplay.eval_pool_size=8
    selfplay.eval_metric=money
    selfplay.magnet_path=None
    env.reward_potential_scale=0.5
    env.reward_potential_gamma=0.99970
    env.reward_money_scale=0
    env.bot_opponent_fraction=0.5
    env.bot_pass_fraction=0
    env.bot_first=0
    env.bot_top_fraction=0
    env.bot_rules_fraction=0.25
    env.bot_script_fraction=0.3
    env.bot_adaptive_fraction=0.7
    train.total_timesteps="$steps"
    train.learning_rate=0.000188
    train.anneal_lr=1
    train.gamma=0.99970
    train.gae_lambda=0.98745
    train.reward_clip=0
    train.ent_coef=0.000334
    train.emag_kl_coef=0
    train.minibatch_size=4096
    train.horizon=128
)

run_variant() {
    local variant=$1 cash_scale=$2
    local run_id="elite_densecash_${variant}_1024x2_${stamp}"
    local run_dir="checkpoints/kaggriculture/$run_id"
    local final_checkpoint

    printf '\nRUN variant=%s cash_scale=%s steps=%s source=%s\n' \
        "$variant" "$cash_scale" "$steps" "$source_checkpoint" | tee -a "$queue_log"
    ./puffer train kaggriculture "${common[@]}" \
        "base.run_id=$run_id" \
        "env.reward_cash_scale=$cash_scale" 2>&1 | tee -a "$queue_log"

    final_checkpoint=$(find "$run_dir" -maxdepth 1 -type f \
        -regextype posix-extended -regex '.*/[0-9]{16}\.bin' -print \
        | sort | tail -n 1)
    [[ -n $final_checkpoint ]] || {
        printf 'No final checkpoint found for %s\n' "$run_id" >&2
        exit 1
    }
    actual_bytes=$(stat -c %s "$final_checkpoint")
    [[ $actual_bytes == "$expected_bytes" ]] || {
        printf 'Final checkpoint architecture mismatch: %s\n' "$final_checkpoint" >&2
        exit 1
    }
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$variant" "$run_id" "$source_checkpoint" "$cash_scale" "$steps" \
        "$final_checkpoint" >> "$manifest"
    printf 'FINAL variant=%s run=%s checkpoint=%s\n' \
        "$variant" "$run_id" "$final_checkpoint" | tee -a "$queue_log"
}

run_variant w1 1
run_variant w2 2

printf '\nDENSE CASH FOLLOW-UP COMPLETE manifest=%s\n' "$manifest" | tee -a "$queue_log"
