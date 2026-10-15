#!/usr/bin/env bash
# Small paired learning pilot, not a hyperparameter sweep or speed benchmark.
set -euo pipefail
cd "$(dirname "$0")/.."
transformer_binary=${GOOFSPIEL_TRAIN_BINARY:-./build/puffer_goofspiel_transformer}
transformer_solver=${GOOFSPIEL_EXACT_GPU:-./ocean/goofspiel/build/exploit_gpu}
if [[ $# != 1 || -e $1 ]]; then
    printf 'Usage: %s NEW_REPORT_DIRECTORY\n' "$0" >&2
    exit 1
fi
transformer_report=$1
[[ -x $transformer_binary && -x $transformer_solver ]]
mkdir -p "$transformer_report"
transformer_common=(
    --base.checkpoint_dir="$transformer_report/checkpoints"
    --base.log_dir="$transformer_report/logs"
    --base.eval_episodes=0 --base.checkpoint_interval=16 --base.load_model_path=None
    --sweep.metric=score # Historical pilot measures exact exploitability offline.
    --base.async=1 --base.cudagraphs=1
    --vec.total_agents=512 --vec.num_buffers=2 --vec.num_threads=4
    --vec.num_policies=5 --vec.hist_policy_percent=.5
    --selfplay.enabled=1 --selfplay.initial_opponents=None
    --selfplay.opp_timeout_steps=65536 --selfplay.eval_games=0 --selfplay.eval_bot_games=0
    --policy.hidden_size=32 --policy.num_layers=2 --policy.transformer_heads=2
    --env.exact_exploiter=0 --train.total_timesteps=262144 --train.horizon=8
    --train.minibatch_size=512 --train.replay_ratio=4
)
for transformer_seed in 73 74 75; do
    # Alternate order to reduce (not eliminate) clock/thermal order bias.
    transformer_models=(puffernet transformer)
    [[ $transformer_seed != 74 ]] || transformer_models=(transformer puffernet)
    for transformer_model in "${transformer_models[@]}"; do
        transformer_id="${transformer_model}_s${transformer_seed}"
        printf 'Training %s (262144 steps, CPU simulator / GPU learner)\n' "$transformer_id"
        /usr/bin/time -f '%e' -o "$transformer_report/$transformer_id.process_seconds.txt" \
            "$transformer_binary" train "${transformer_common[@]}" \
            --base.run_id="$transformer_id" --policy.architecture="$transformer_model" \
            --base.seed="$transformer_seed" --env.seed="$transformer_seed" --selfplay.seed="$transformer_seed" \
            > "$transformer_report/$transformer_id.log" 2>&1
        transformer_checkpoints=("$transformer_report/checkpoints/goofspiel/$transformer_id/"*.bin)
        [[ ${#transformer_checkpoints[@]} == 5 ]]
        "$transformer_solver" "${transformer_checkpoints[@]}" \
            --policy.architecture="$transformer_model" --policy.hidden_size=32 \
            --policy.num_layers=2 --policy.transformer_heads=2 \
            > "$transformer_report/$transformer_id.exploitability.tsv"
        printf 'Finished %s; checkpoints and exact exploitabilities saved.\n' "$transformer_id"
    done
done
