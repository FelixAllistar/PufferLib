#!/usr/bin/env bash
# Run only in the isolated candidate checkout, after prepare_performance.sh.
set -euo pipefail
cd "$(dirname "$0")/../../.."
kag_out=$(realpath "${1:?Usage: run_performance.sh QUALIFICATION_DIRECTORY [BASELINE_RESULTS]}")
kag_reference=$(realpath "${2:-$kag_out}")
[[ -x "$kag_reference/gpu_baseline" && -x "$kag_out/gpu_candidate" ]]
cmp config/kaggriculture.ini "$kag_reference/active-config.ini"
kag_variants=(baseline candidate)
if [[ "$kag_reference" != "$kag_out" ]]; then
    kag_variants=(candidate)
    cp "$kag_reference/active-config.ini" "$kag_out/active-config.ini"
fi
idle_gpu() {
    if [[ -n $(nvidia-smi --query-compute-apps=pid --format=csv,noheader) ]]; then
        echo 'GPU is busy; no qualification job launched.' >&2; exit 1;
    fi
}
for kag_case in graphs eager single fresh raw legacy macro_default macro_explicit mode2_original task; do
    kag_flags=(base.cudagraphs=1 vec.num_frozen_banks=8 vec.frozen_bank_pct=0.75
        env.macro_mode=2 env.macro_executor_version=2 env.reset_state_prob=0.8)
    case "$kag_case" in
        eager) kag_flags+=(base.cudagraphs=-1);;
        single) kag_flags+=(vec.num_frozen_banks=0 vec.frozen_bank_pct=0);;
        fresh) kag_flags+=(env.reset_state_prob=0);;
        raw) kag_flags+=(env.macro_mode=0 env.macro_executor_version=0);;
        legacy) kag_flags+=(env.macro_executor_version=1);;
        macro_default) kag_flags+=(env.macro_mode=1 env.macro_executor_version=0 env.macro_decision_interval=4);;
        macro_explicit) kag_flags+=(env.macro_mode=1 env.macro_executor_version=1 env.macro_decision_interval=4);;
        mode2_original) kag_flags+=(env.macro_executor_version=0);;
        task) kag_flags+=(env.macro_mode=3 env.macro_executor_version=0);;
    esac
    kag_case_variants=("${kag_variants[@]}")
    # A later candidate can add regression cases without rerunning completed
    # baselines. New baseline dumps still use the archived original binary.
    if [[ "$kag_reference" != "$kag_out" && ! -f "$kag_reference/${kag_case}_baseline.bin" ]]; then
        kag_case_variants=(baseline candidate)
    fi
    for kag_variant in "${kag_case_variants[@]}"; do
        idle_gpu
        kag_case_out=$kag_out
        [[ "$kag_variant" == baseline ]] && kag_case_out=$kag_reference
        timeout 180s "$kag_case_out/gpu_$kag_variant" parity "$kag_case_out/${kag_case}_${kag_variant}.bin" \
            base.load_model_path=None base.async=0 base.profile=0 base.seed=5 env.seed=707 \
            policy.hidden_size=64 policy.num_layers=2 vec.total_agents=256 vec.num_buffers=1 \
            vec.frozen_bank_hidden_size=32 vec.frozen_bank_num_layers=1 \
            train.horizon=8 train.minibatch_size=32 train.total_timesteps=8192 train.emag_kl_coef=0 \
            env.frozen_macro_mode=-1 env.frozen_macro_executor_version=-1 \
            "${kag_flags[@]}" 2>&1 | tee "$kag_case_out/${kag_case}_${kag_variant}.log"
    done
    cmp "$kag_reference/${kag_case}_baseline.bin" "$kag_out/${kag_case}_candidate.bin"
    echo "PASS exact GPU rollout parity: $kag_case"
done
for kag_variant in "${kag_variants[@]}"; do
    idle_gpu
    # Matched full H512/L3 model, original 2048x720 geometry, 8 opponents,
    # resets ON. Three complete PPO updates; no evaluation or long run.
    timeout 1200s "$kag_out/gpu_$kag_variant" train kaggriculture \
        "base.run_id=perf_$kag_variant" "base.checkpoint_dir=$kag_out/checkpoints" \
        "base.log_dir=$kag_out/logs" base.load_model_path=None base.checkpoint_interval=1 \
        base.eval_epoch_mult=0 base.perf_log=1 base.async=0 base.cudagraphs=1 \
        base.seed=5 env.seed=707 selfplay.eval_pool_size=0 sweep.downsample=1 \
        policy.hidden_size=512 policy.num_layers=3 \
        vec.total_agents=2048 vec.num_buffers=1 vec.num_frozen_banks=8 vec.frozen_bank_pct=0.75 \
        vec.frozen_bank_hidden_size=512 vec.frozen_bank_num_layers=3 \
        env.macro_mode=2 env.macro_executor_version=2 env.reset_state_prob=0.8 \
        env.frozen_macro_mode=-1 env.frozen_macro_executor_version=-1 \
        train.horizon=720 train.minibatch_size=2880 train.total_timesteps=4423680 \
        train.emag_kl_coef=0 train.gpus=1 2>&1 | tee "$kag_out/train_$kag_variant.log"
done
for kag_steps in 0000000001474560 0000000002949120 0000000004423680; do
    cmp "$kag_reference/checkpoints/kaggriculture/perf_baseline/$kag_steps.bin" \
        "$kag_out/checkpoints/kaggriculture/perf_candidate/$kag_steps.bin"
done
echo 'PASS: exact GPU rollout parity and all three PPO checkpoint byte comparisons.'
