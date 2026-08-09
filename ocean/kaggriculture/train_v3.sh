#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

behavior_steps=${1:-6500000}
economy_steps=${2:-100000000}
archive=saved/kaggriculture_v3
mkdir -p "$archive"

latest_checkpoint() {
    local run=$1
    find "checkpoints/kaggriculture/$run" -maxdepth 1 -type f -name '*.bin' \
        -printf '%f %p\n' | sort -r | sed -n '1s/^[^ ]* //p'
}

run_train() {
    ./puffer train kaggriculture \
        policy.hidden_size=32 policy.num_layers=2 \
        vec.frozen_bank_hidden_size=32 vec.frozen_bank_num_layers=2 \
        env.bot_opponent_fraction=0.75 \
        selfplay.enabled=1 selfplay.pfsp_mode=variance \
        selfplay.pfsp_uniform_mix=0.10 \
        train.learning_rate=0.0002 train.ent_coef=0.006 \
        "$@"
}

stamp=$(date +%s%3N)
behavior_run="kag_v3_behavior_$stamp"
printf 'V3 behavior bootstrap: %s steps, run=%s\n' \
    "$behavior_steps" "$behavior_run"
run_train \
    "base.run_id=$behavior_run" base.load_model_path=None \
    selfplay.opponent_pool=None selfplay.opponent_pool_prob=0 \
    selfplay.snapshot_interval=2000000 \
    env.reward_potential_scale=0.0001 env.reward_win=0.1 \
    env.reward_productive_action=0.01 env.reward_inactivity=3 \
    env.reward_neglect_death=0.05 \
    "train.total_timesteps=$behavior_steps"
behavior_checkpoint=$(latest_checkpoint "$behavior_run")
[[ -n $behavior_checkpoint ]] || {
    printf 'No behavior checkpoint produced by %s\n' "$behavior_run" >&2
    exit 1
}
cp "$behavior_checkpoint" "$archive/behavior.bin"

stamp=$(date +%s%3N)
economy_run="kag_v3_economy_$stamp"
printf 'V3 economic transfer: %s steps, run=%s\n' \
    "$economy_steps" "$economy_run"
run_train \
    "base.run_id=$economy_run" \
    "base.load_model_path=$archive/behavior.bin" \
    "selfplay.opponent_pool=$archive/behavior.bin" \
    selfplay.opponent_pool_prob=0.25 selfplay.snapshot_interval=5000000 \
    env.reward_potential_scale=0.000772047148 env.reward_win=0.375989795 \
    env.reward_productive_action=0.0002 env.reward_inactivity=3 \
    env.reward_neglect_death=0.05 \
    train.ent_coef=0.003 "train.total_timesteps=$economy_steps"
economy_checkpoint=$(latest_checkpoint "$economy_run")
[[ -n $economy_checkpoint ]] || {
    printf 'No economy checkpoint produced by %s\n' "$economy_run" >&2
    exit 1
}

printf 'V3 candidates complete\nbehavior=%s\neconomy_run=%s\n' \
    "$archive/behavior.bin" "checkpoints/kaggriculture/$economy_run"
printf 'The economy final is intentionally not promoted. Rank all snapshots with:\n'
printf './ocean/kaggriculture/eval_v3.sh %s 50\n' "$economy_run"
