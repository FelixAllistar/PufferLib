#!/usr/bin/env bash
set -euo pipefail

# Three clean branches from the transferable S7 peak. These intentionally
# vary credit-assignment and reset distribution, while holding the reward and
# opponent league fixed.
ROOT="${KAG_ROOT:-/workspace/PufferLib}"
FULL_BANK="${KAG_FULL_BANK:-/workspace/elite_replays/state_bank/full_1365_each.kgb}"
EARLY_MID_BANK="${KAG_EARLY_MID_BANK:-/workspace/elite_replays/state_bank/full_early_mid_t0_480.kgb}"
SOURCE="${KAG_SOURCE:-checkpoints/kaggriculture/fullbank_s7_competitive_mix_512x2_v1/0000000352321536.bin}"
STEPS="${KAG_VARIANT_STEPS:-200000000}"
PYTHON="${KAG_PYTHON:-/venv/main/bin/python}"

cd "$ROOT"

if [[ ! -s "$EARLY_MID_BANK" ]]; then
    "$PYTHON" ocean/kaggriculture/slice_replay_state_bank.py \
        --bank "$FULL_BANK" --output "$EARLY_MID_BANK" \
        --stage full --min-turn 0 --max-turn 480
fi

train_variant() {
    local run="$1"
    local bank="$2"
    local reset_prob="$3"
    local horizon="$4"
    local replay_ratio="$5"
    local learning_rate="$6"
    local entropy="$7"
    local agents="$8"
    if [[ -d "checkpoints/kaggriculture/$run" ]]; then
        echo "Refusing to overwrite existing run: $run" >&2
        return 1
    fi
    echo "START $run bank=$(basename "$bank") reset=$reset_prob horizon=$horizon replay=$replay_ratio agents=$agents"
    ./puffer train kaggriculture \
        base.run_id="$run" \
        base.load_model_path="$SOURCE" \
        base.cudagraphs=0 \
        selfplay.magnet_path="$SOURCE" \
        env.reset_state_bank="$bank" \
        env.reset_state_prob="$reset_prob" \
        vec.total_agents="$agents" \
        train.total_timesteps="$STEPS" \
        train.horizon="$horizon" \
        train.replay_ratio="$replay_ratio" \
        train.learning_rate="$learning_rate" \
        train.ent_coef="$entropy" \
        train.emag_kl_coef=0.001 \
        train.emag_tau=0
    echo "DONE $run"
}

# Longer recurrent credit assignment plus two PPO passes.
train_variant fullbank_s8_h256_rr2_a4096_512x2_v1 "$FULL_BANK" 0.15 256 2 0.00012 0.0006 4096

# Spend much more time in verified early/mid elite states, while retaining
# enough real roots to test whether partial-game skills join into a full loop.
train_variant fullbank_s8_earlymid40_512x2_v1 "$EARLY_MID_BANK" 0.40 128 1 0.00018 0.0010 8192

# Frequent updates and mostly-root play, with only a small replay rehearsal.
train_variant fullbank_s8_h64_rr2_root95_512x2_v1 "$FULL_BANK" 0.05 64 2 0.00010 0.0005 8192

echo "FULLBANK VARIANT SEARCH COMPLETE"
