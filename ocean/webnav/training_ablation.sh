#!/usr/bin/env bash
# Reproduce a matched-budget experiment after building the native tools/trainer.
set -euo pipefail
cd "$(dirname "$0")/../.."
run_dir=${1:-build/webnav/training/ablation_$(date +%Y%m%d_%H%M%S)}
steps=${2:-5000000}
[[ "$steps" =~ ^[0-9]+$ ]] && [ "$steps" -ge 2048 ] || { echo 'steps must be an integer >= 2048' >&2; exit 2; }
for executable in puffer_dom training_eval training_browser test_training_policy; do
    test -x "build/webnav/$executable" || { echo "Build build/webnav/$executable first; see TRAINING.md" >&2; exit 1; }
done
# Refuse an existing destination so old checkpoints cannot contaminate results.
mkdir "$run_dir"
for potion in 0 1; do
    label=lexical
    [ "$potion" = 0 ] || label=potion
    /usr/bin/time -p build/webnav/puffer_dom train webnav_dom \
        "env.potion=$potion" "train.total_timesteps=$steps" base.checkpoint_interval=1000 \
        "base.checkpoint_dir=$run_dir/$label" "base.log_dir=$run_dir/logs" \
        > "$run_dir/$label.log" 2>&1
    sidecar=$(find "$run_dir/$label" -name '*.bin.webnav.json' | sort | tail -n 1)
    checkpoint=${sidecar%.webnav.json}
    build/webnav/test_training_policy "$checkpoint" > "$run_dir/$label-policy.log" 2>&1
    build/webnav/training_eval "$checkpoint" 100 > "$run_dir/$label-eval.jsonl" 2> "$run_dir/$label-eval.log"
    for task in click-button click-link focus-text click-checkboxes click-option; do
        build/webnav/training_browser "$checkpoint" "$task" 20
    done > "$run_dir/$label-browser.jsonl" 2> "$run_dir/$label-browser.log"
done
node ocean/webnav/tools/training_report.cjs "$run_dir" lexical potion > "$run_dir/results.json"
echo "Results: $run_dir/results.json"
