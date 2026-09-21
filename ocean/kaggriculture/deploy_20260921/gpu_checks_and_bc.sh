#!/usr/bin/env bash
# Bounded online integration and paired replay-BC pilots, never a sweep.
set -euo pipefail
cd /workspace/PufferLib
if pgrep -x puffer >/dev/null || pgrep -x kag_bc >/dev/null; then
    echo 'Another GPU trainer is running; refusing concurrent jobs.' >&2
    exit 1
fi
kag_out=$(mktemp -d /workspace/PufferLib/qualification/production_20260921/pilot.XXXXXX)
kag_tag=${kag_out##*/}
exec > >(tee "$kag_out/pipeline.log") 2>&1
printf 'BOUNDED GPU CHECKS + REPLAY BC\nOutputs: %s\n' "$kag_out"
cp config/kaggriculture.ini "$kag_out/active-config.ini"
kag_profile=ocean/kaggriculture/bc_2_2_baseline_20260921.ini
kag_data=data/entity_2_2_policy5_baseline_64g_v1.bc
sha256sum puffer ocean/kaggriculture/build/kag_bc "$kag_data" \
    > "$kag_out/input-sha256.txt"
./puffer check kaggriculture
ocean/kaggriculture/build/kag_bc "bc.data=$kag_data" bc.verify_only=1 bc.value_coef=0.1
echo 'PPO checkpoint-load smoke: real reset bank, reset probability 0.8, 256 agents, two updates.'
./puffer train kaggriculture "base.run_id=multi22_${kag_tag}_small" \
    base.load_model_path=qualification/baseline_v2_checks/joint.bin \
    vec.total_agents=256 train.total_timesteps=368640 \
    base.checkpoint_interval=1 base.eval_epoch_mult=0 selfplay.eval_pool_size=0 \
    sweep.downsample=1 base.perf_log=1 2>&1 | tee "$kag_out/ppo_small.log"
echo 'Production geometry check: your 2048 agents, 8 frozen banks, two updates.'
./puffer train kaggriculture "base.run_id=multi22_${kag_tag}_full" \
    base.load_model_path=qualification/baseline_v2_checks/joint.bin \
    train.total_timesteps=2949120 base.checkpoint_interval=1 \
    base.eval_epoch_mult=0 selfplay.eval_pool_size=0 sweep.downsample=1 \
    base.perf_log=1 2>&1 | tee "$kag_out/ppo_full.log"
echo 'Replay BC comparison: same initialization, learning rate, data and 100 epochs.'
for kag_variant in actor joint; do
    kag_value=0
    [[ "$kag_variant" == joint ]] && kag_value=0.1
    ocean/kaggriculture/build/kag_bc bc.mode=train "bc.profile=$kag_profile" \
        "bc.data=$kag_data" bc.verify_only=0 bc.epochs=100 \
        bc.seed=7 bc.batch=1 bc.learning_rate=0.00005 bc.report_interval=10 \
        bc.load_model_path=None "bc.value_coef=$kag_value" \
        "bc.output=$kag_out/${kag_variant}_100.bin" 2>&1 | tee "$kag_out/${kag_variant}_100.log"
    echo "Closed-loop ${kag_variant} evaluation: 64 full games per seat, fixed rules-bot mix, resets OFF."
    for kag_seat in 0 1; do
    ./puffer eval_bot kaggriculture "base.load_model_path=$kag_out/${kag_variant}_100.bin" \
        base.num_games=64 base.eval_agents=64 base.eval_deterministic=1 \
        base.seed=5 env.seed=707 "env.bot_first=$kag_seat" \
        env.reset_state_prob=0 env.reset_opening_prob=0 \
        env.bot_opponent_fraction=1 env.bot_pass_fraction=0 env.bot_top_fraction=0 \
        env.bot_rules_fraction=1 env.bot_script_fraction=0 env.bot_adaptive_fraction=0 \
        2>&1 | tee "$kag_out/${kag_variant}_100_eval_seat${kag_seat}.log"
    done
done
echo 'PASS: active-install PPO integration and both replay-BC pilots completed.' \
    | tee "$kag_out/PASS.txt"
echo 'No long PPO job or sweep was launched. Pilot models are not automatically selected as defaults.'
