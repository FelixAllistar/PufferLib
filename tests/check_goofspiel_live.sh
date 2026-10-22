#!/usr/bin/env bash
# Real, bounded CPU-environment/GPU-training checks. All artifacts stay in NEW_REPORT.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# != 1 || -e $1 ]]; then
    printf 'Usage: %s NEW_REPORT_DIRECTORY\n' "$0" >&2
    exit 1
fi
goof_report=$1
goof_binary=${GOOFSPIEL_TRAIN_BINARY:-./build/puffer_goofspiel_transformer}
goof_solver=${GOOFSPIEL_EXACT_GPU:-./ocean/goofspiel/build/exploit_gpu}
mkdir -p "$goof_report"
goof_common=(
    --base.checkpoint_dir="$goof_report/checkpoints" --base.log_dir="$goof_report/logs"
    --base.load_model_path=None --base.async=1 --base.cudagraphs=1
    --vec.total_agents=64 --vec.num_buffers=2 --vec.num_threads=2
    --policy.hidden_size=16 --policy.num_layers=1
    --train.total_timesteps=4096 --train.horizon=8 --train.minibatch_size=128 --train.replay_ratio=1
    --env.exploitability_interval_steps=1024
    --sweep.early_stop_min_steps=2048 --sweep.early_stop_patience_steps=1024
    --sweep.early_stop_min_delta=1
)

for goof_model in puffernet transformer; do
    "$goof_binary" train "${goof_common[@]}" --base.run_id="$goof_model" \
        --policy.architecture="$goof_model" > "$goof_report/$goof_model.log" 2>&1
    [[ $(grep -c '^Exact checkpoint:' "$goof_report/$goof_model.log") == 5 ]]
    grep -q '^Goofspiel result:' "$goof_report/$goof_model.log"
    [[ -s "$goof_report/checkpoints/goofspiel/$goof_model/0000000000004096.bin.ini" ]]
    # No explicit architecture or dimensions: recover them from the saved settings.
    env -u DISPLAY "$goof_binary" eval latest \
        --base.checkpoint_dir="$goof_report/checkpoints" --base.log_dir="$goof_report/logs" \
        > "$goof_report/$goof_model.latest.txt"
    grep -q "architecture=$goof_model width=16 layers=1" "$goof_report/$goof_model.latest.txt"
    goof_expected=$(awk -F ' = ' '$1=="env/exploitability" {print $2}' "$goof_report/logs/goofspiel/$goof_model.ini")
    goof_actual=$(awk '/^exploitability=/ {split($1,a,"="); print a[2]}' "$goof_report/$goof_model.latest.txt")
    awk -v a="$goof_actual" -v b="$goof_expected" 'BEGIN { d=a-b; if (d<0) d=-d; exit !(a!="" && b!="" && d<0.0000005) }'
    printf 'PASS real %s training, periodic/final exact metric, metadata and eval latest\n' "$goof_model"
done

"$goof_solver" latest --base.checkpoint_dir="$goof_report/checkpoints" \
    --base.log_dir="$goof_report/logs" > "$goof_report/standalone.latest.txt"
goof_actual=$(awk '/^nash_conv=/ {split($2,a,"="); print a[2]}' "$goof_report/standalone.latest.txt")
awk -v a="$goof_actual" -v b="$goof_expected" 'BEGIN { d=a-b; if (d<0) d=-d; exit !(a!="" && b!="" && d<0.0000005) }'
printf 'PASS standalone latest agrees with automatic training metric\n'

"$goof_binary" train "${goof_common[@]}" --base.run_id=mirror_final_only \
    --selfplay.enabled=0 --base.checkpoint_interval=0 > "$goof_report/mirror_final_only.log" 2>&1
[[ $(grep -c '^Exact checkpoint:' "$goof_report/mirror_final_only.log") == 1 ]]
grep -q '^Goofspiel result:' "$goof_report/mirror_final_only.log"
printf 'PASS final-only automatic exact evaluation without frozen opponents\n'

"$goof_binary" train "${goof_common[@]}" --base.run_id=response_pool \
    --env.exact_exploiter=1 --env.exact_exploiter_history=3 > "$goof_report/response_pool.log" 2>&1
[[ $(grep -c '^Exact checkpoint:' "$goof_report/response_pool.log") == 5 ]]
[[ -s "$goof_report/checkpoints/goofspiel/response_pool/0000000000004096.bin.exact" ]]
printf 'PASS exact-response opponent pool remains compatible\n'

for goof_model in puffernet transformer; do
    "$goof_binary" sweep \
        --base.checkpoint_dir="$goof_report/sweep_$goof_model/checkpoints" \
        --base.log_dir="$goof_report/sweep_$goof_model/logs" \
        --policy.architecture="$goof_model" --policy.hidden_size=16 --policy.num_layers=1 \
        --vec.total_agents=512 --vec.num_buffers=1 --vec.num_threads=2 \
        --train.total_timesteps=32768 --train.horizon=8 --train.minibatch_size=512 --train.replay_ratio=1 \
        --env.exploitability_interval_steps=8192 --sweep.max_runs=3 \
        --sweep.vec.total_agents.min=256 --sweep.vec.total_agents.max=1024 \
        --sweep.vec.num_buffers.max=4 --sweep.vec.num_threads.max=2 \
        --sweep.train.horizon.max=16 --sweep.train.minibatch_size.min=128 --sweep.train.minibatch_size.max=512 \
        --sweep.train.replay_ratio.min=.5 --sweep.train.replay_ratio.max=2 \
        --sweep.policy.hidden_size.max=32 --sweep.policy.num_layers.max=2 \
        > "$goof_report/sweep_$goof_model.log" 2>&1
    [[ $(grep -c '^sweep run=' "$goof_report/sweep_$goof_model.log") == 3 ]]
    if grep -q 'worker .*failed' "$goof_report/sweep_$goof_model.log"; then exit 1; fi
    # Every objective is the minimum of the exact saved-checkpoint measurements.
    while IFS= read -r goof_settings; do
        goof_run=${goof_settings##*/}; goof_run=${goof_run%.ini}
        goof_path="$goof_report/sweep_$goof_model/checkpoints/goofspiel/$goof_run"
        goof_h=$(awk -F ' = ' '/^\[policy\]/{s=1;next} /^\[/{s=0} s && $1=="hidden_size" {print $2}' "$goof_settings")
        goof_l=$(awk -F ' = ' '/^\[policy\]/{s=1;next} /^\[/{s=0} s && $1=="num_layers" {print $2}' "$goof_settings")
        goof_heads=$(awk -F ' = ' '/^\[policy\]/{s=1;next} /^\[/{s=0} s && $1=="transformer_heads" {print $2}' "$goof_settings")
        "$goof_solver" "$goof_path"/*.bin --policy.architecture="$goof_model" \
            --policy.hidden_size="$goof_h" --policy.num_layers="$goof_l" \
            --policy.transformer_heads="$goof_heads" > "$goof_path/exact_check.txt"
        goof_best=$(awk -F '[=\t]' '/^best=/ {print $2}' "$goof_path/exact_check.txt")
        goof_logged=$(awk -F ' = ' '$1=="env/best_exploitability" {print $2}' "$goof_settings")
        awk -v a="$goof_best" -v b="$goof_logged" 'BEGIN { d=a-b; if (d<0) d=-d; exit !(a!="" && b!="" && d<0.0000005) }'
    done < <(find "$goof_report/sweep_$goof_model/logs/goofspiel" -type f -name '*.ini')
    printf 'PASS real %s sweep: three trials, exact objective verified independently\n' "$goof_model"
done

for goof_model in puffernet transformer; do
    "$goof_binary" sweep "${goof_common[@]}" --policy.architecture="$goof_model" \
        --base.checkpoint_dir="$goof_report/stopped_$goof_model/checkpoints" \
        --base.log_dir="$goof_report/stopped_$goof_model/logs" --sweep.max_runs=1 \
        --sweep.vec.total_agents.min=64 --sweep.train.minibatch_size.min=128 \
        --sweep.train.replay_ratio.min=1 > "$goof_report/stopped_$goof_model.log" 2>&1
    grep -q '^Goofspiel early stop: step=2048 ' "$goof_report/stopped_$goof_model.log"
    grep -q '^sweep run=0 .*steps=2560 ' "$goof_report/stopped_$goof_model.log"
    goof_settings=("$goof_report/stopped_$goof_model/logs/goofspiel/"*.ini)
    grep -q '^env/early_stopped = 1$' "${goof_settings[0]}"
    printf 'PASS %s sweep stops cleanly, returns actual steps and preserves checkpoints\n' "$goof_model"
done
goof_preflight=(
    --policy.hidden_size=16 --policy.num_layers=1
    --vec.total_agents=8192 --vec.num_buffers=1 --vec.num_threads=2
    --train.total_timesteps=131072 --train.horizon=8 --train.minibatch_size=8192
    --sweep.max_runs=1
)
goof_bad=(
    '--sweep.memory_limit_mb=1'
    '--selfplay.enabled=0 --env.exact_exploiter=1'
    '--vec.num_policies=2 --env.exact_exploiter=1 --env.exact_exploiter_banks=2'
    '--train.vtrace=1 --train.vtrace_rho_clip=.5 --train.vtrace_c_clip=1'
    '--vec.hist_policy_percent=.375'
    '--sweep.max_train_graph_work=1'
)
goof_reasons=('estimated training/evaluation memory' 'exact_exploiter requires'
    'enough frozen' 'vtrace_c_clip' 'divide learner rows' 'CUDA training graph exceeds')
for goof_index in "${!goof_bad[@]}"; do
    read -ra goof_overrides <<< "${goof_bad[$goof_index]}"
    if "$goof_binary" sweep "${goof_preflight[@]}" "${goof_overrides[@]}" \
        --base.checkpoint_dir="$goof_report/preflight_$goof_index/checkpoints" \
        --base.log_dir="$goof_report/preflight_$goof_index/logs" \
        > "$goof_report/preflight_$goof_index.log" 2>&1; then
        printf 'Invalid preflight configuration accepted: %s\n' "${goof_bad[$goof_index]}" >&2
        exit 1
    fi
    rg -q "${goof_reasons[$goof_index]}" "$goof_report/preflight_$goof_index.log"
    [[ ! -e "$goof_report/preflight_$goof_index/checkpoints" ]]
    [[ ! -e "$goof_report/preflight_$goof_index/logs" ]]
done
printf 'PASS memory, graph work, selfplay/exact, V-trace and layout rejection before workers\n'
printf 'All real Goofspiel automatic-evaluation checks passed. Reports: %s\n' "$goof_report"
