#!/usr/bin/env bash
# 512x3 two-stage bootstrap with the DAgger'd thunder clone as BOTH the warm
# start and the frozen EMAg magnet. Phase 1 uses high entropy plus dense chore
# shaping to keep the fresh big policy out of the "keep 3000" attractor while
# the magnet supplies a known-good opening. Phase 2 transfers to the v19
# economic potential objective and tightens entropy.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

behavior_steps=${1:-20000000}
economy_steps=${2:-400000000}
hidden=512
layers=3
clone=saved/kaggriculture_bc_v2/thunder_dagger5_h512_l3.bin
archive=saved/kaggriculture_v3_h512_l3_magnet
mkdir -p "$archive"

[[ -f $clone ]] || { echo "clone not found: $clone" >&2; exit 1; }

latest_checkpoint() {
    local run=$1
    find "checkpoints/kaggriculture/$run" -maxdepth 1 -type f -name '*.bin' \
        -printf '%f %p\n' | sort -r | sed -n '1s/^[^ ]* //p'
}

run_train() {
    ./puffer train kaggriculture \
        "policy.hidden_size=$hidden" "policy.num_layers=$layers" \
        "vec.frozen_bank_hidden_size=$hidden" \
        "vec.frozen_bank_num_layers=$layers" \
        "selfplay.magnet_path=$clone" \
        train.emag_kl_coef=0.0121208057 \
        train.emag_tau=0 train.emag_cutoff=0.5 \
        selfplay.enabled=1 selfplay.pfsp_mode=variance \
        selfplay.pfsp_uniform_mix=0.10 \
        train.learning_rate=0.0002 \
        "$@"
}

stamp=$(date +%s%3N)
behavior_run="kag_v3h512m_behavior_$stamp"
printf 'V3 512x3+magnet behavior bootstrap: %s steps, run=%s\n' \
    "$behavior_steps" "$behavior_run"
run_train \
    "base.run_id=$behavior_run" "base.load_model_path=$clone" \
    selfplay.opponent_pool=None selfplay.opponent_pool_prob=0 \
    selfplay.snapshot_interval=2000000 \
    env.reward_potential_scale=0.0001 env.reward_win=0.1 \
    env.reward_productive_action=0.01 env.reward_inactivity=3 \
    env.reward_neglect_death=0.05 \
    train.ent_coef=0.006 "train.total_timesteps=$behavior_steps"
behavior_checkpoint=$(latest_checkpoint "$behavior_run")
[[ -n $behavior_checkpoint ]] || {
    printf 'No behavior checkpoint produced by %s\n' "$behavior_run" >&2
    exit 1
}
cp "$behavior_checkpoint" "$archive/behavior.bin"

stamp=$(date +%s%3N)
economy_run="kag_v3h512m_economy_$stamp"
printf 'V3 512x3+magnet economic transfer: %s steps, run=%s\n' \
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

printf 'V3 512x3+magnet candidates complete\nbehavior=%s\neconomy_run=%s\n' \
    "$archive/behavior.bin" "checkpoints/kaggriculture/$economy_run"
printf 'Rank snapshots with: ./ocean/kaggriculture/eval_population.sh --games 50 --sample-run 16 --gpu-agents 64 checkpoints/kaggriculture/%s saved/kaggriculture_league_v6\n' \
    "$economy_run"
