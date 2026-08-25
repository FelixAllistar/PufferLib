#!/usr/bin/env bash
set -euo pipefail

# Continue the verified replay-reset curriculum without replaying raw episodes.
# The script waits for the master bank and the test3 deterministic/stochastic
# screens, then promotes the best root-evaluated checkpoint between stages.

ROOT="${KAG_ROOT:-/workspace/PufferLib}"
BANK="${KAG_FULL_BANK:-/workspace/elite_replays/state_bank/full_1365_each.kgb}"
BANK_BYTES="${KAG_FULL_BANK_BYTES:-814682176}"
GAMES="${KAG_PROMOTION_GAMES:-30}"
GPU_AGENTS="${KAG_GPU_AGENTS:-64}"
SEED_A="${KAG_FIXED_SEED_A:-12000}"
SEED_B="${KAG_FIXED_SEED_B:-13000}"

cd "$ROOT"

wait_for_file() {
    local path="$1"
    while [ ! -s "$path" ]; do sleep 30; done
}

wait_for_bank() {
    while [ ! -f "$BANK" ] || [ "$(stat -c %s "$BANK" 2>/dev/null || echo 0)" != "$BANK_BYTES" ]; do
        sleep 30
    done
}

checkpoint_for_policy() {
    local prefix="$1"
    local policy
    policy="$(awk -F '\t' 'NR == 2 {print $2}' "${prefix}_ranking.tsv")"
    awk -F '\t' -v policy="$policy" 'NR > 1 && $2 == policy {print $3; exit}' "${prefix}_manifest.tsv"
}

evaluate_run() {
    local run="$1"
    local tag="$2"
    local dir="checkpoints/kaggriculture/$run"
    local det="logs/kaggriculture/${tag}_det"
    local stoch="logs/kaggriculture/${tag}_stoch"

    KAG_FIXED_SEED_A="$SEED_A" KAG_FIXED_SEED_B="$SEED_B" \
        ./ocean/kaggriculture/eval_population.sh \
        --games "$GAMES" --gpu-agents "$GPU_AGENTS" --sample-run 8 \
        --fixed pass,rules --output "$det" "$dir" >&2
    KAG_FIXED_SEED_A="$SEED_A" KAG_FIXED_SEED_B="$SEED_B" \
        ./ocean/kaggriculture/eval_population.sh \
        --games "$GAMES" --gpu-agents "$GPU_AGENTS" --sample-run 8 \
        --fixed pass,rules --stochastic --output "$stoch" "$dir" >&2
    checkpoint_for_policy "$det"
}

train_stage() {
    local run="$1"
    local load="$2"
    local reset_prob="$3"
    local steps="$4"

    if [ -d "checkpoints/kaggriculture/$run" ]; then
        echo "Refusing to overwrite existing run: $run" >&2
        exit 1
    fi
    ./puffer train kaggriculture \
        base.run_id="$run" \
        base.load_model_path="$load" \
        selfplay.magnet_path="$load" \
        env.reset_state_bank="$BANK" \
        env.reset_state_prob="$reset_prob" \
        train.total_timesteps="$steps"
}

wait_for_bank
wait_for_file logs/kaggriculture/test3_fullbank_select_det_ranking.tsv
wait_for_file logs/kaggriculture/test3_fullbank_select_stoch_ranking.tsv

source_checkpoint="$(checkpoint_for_policy logs/kaggriculture/test3_fullbank_select_det)"
echo "PROMOTE test3 source=$source_checkpoint"

stage4=reset_s4_fullbank80_512x2_v1
train_stage "$stage4" "$source_checkpoint" 0.80 200000000
source_checkpoint="$(evaluate_run "$stage4" reset_s4_fullbank80_512x2_v1_root)"
echo "PROMOTE $stage4 source=$source_checkpoint"

stage5=reset_s5_fullbank25_512x2_v1
train_stage "$stage5" "$source_checkpoint" 0.25 200000000
source_checkpoint="$(evaluate_run "$stage5" reset_s5_fullbank25_512x2_v1_root)"
echo "PROMOTE $stage5 source=$source_checkpoint"

stage6=reset_s6_root_512x2_v1
train_stage "$stage6" "$source_checkpoint" 0 500000000
source_checkpoint="$(evaluate_run "$stage6" reset_s6_root_512x2_v1_root)"
echo "CURRICULUM COMPLETE best=$source_checkpoint"
