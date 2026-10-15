#!/usr/bin/env bash
# Bounded CPU-simulator / GPU-learner qualification. Never overwrites a run.
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
    --base.eval_episodes=0 --base.checkpoint_interval=0 --base.load_model_path=None
    --vec.total_agents=64 --vec.num_buffers=2 --vec.num_threads=2
    --vec.num_policies=5 --vec.hist_policy_percent=.5
    --selfplay.initial_opponents=None --selfplay.eval_games=0 --selfplay.eval_bot_games=0
    --policy.hidden_size=32 --policy.num_layers=2 --policy.transformer_heads=2
    --env.exact_exploiter=0 --train.total_timesteps=4096 --train.horizon=8
    --train.minibatch_size=128 --train.replay_ratio=1
)
printf 'architecture\tasync\tgraphs\tfrozen_banks\tcheckpoint_bytes\n'
for transformer_model in puffernet transformer; do
    for transformer_async in 0 1; do
        for transformer_graphs in -1 1; do
            for transformer_frozen in 0 1; do
                transformer_id="${transformer_model}_a${transformer_async}_g${transformer_graphs}_f${transformer_frozen}"
                "$transformer_binary" train "${transformer_common[@]}" \
                    --base.run_id="$transformer_id" --policy.architecture="$transformer_model" \
                    --base.async="$transformer_async" --base.cudagraphs="$transformer_graphs" \
                    --selfplay.enabled="$transformer_frozen" \
                    > "$transformer_report/$transformer_id.log" 2>&1
                if grep -Eiq '(^|[[:space:]])[-+]?(nan|inf)([[:space:]]|$)' "$transformer_report/$transformer_id.log"; then
                    printf 'Non-finite training metric: %s\n' "$transformer_id" >&2
                    exit 1
                fi
                transformer_checkpoint="$transformer_report/checkpoints/goofspiel/$transformer_id/0000000000004096.bin"
                transformer_bytes=$(stat -c %s "$transformer_checkpoint")
                transformer_expected=31360
                [[ $transformer_model != transformer ]] || transformer_expected=105216
                [[ $transformer_bytes == "$transformer_expected" ]]
                if [[ $transformer_frozen == 1 ]]; then
                    transformer_initial="$transformer_report/checkpoints/goofspiel/$transformer_id/0000000000000000.bin"
                    if cmp -s "$transformer_initial" "$transformer_checkpoint"; then
                        printf 'Training did not change weights: %s\n' "$transformer_id" >&2
                        exit 1
                    fi
                fi
                # Loading in the solver checks every checkpoint weight is finite.
                "$transformer_solver" "$transformer_checkpoint" \
                    --policy.architecture="$transformer_model" --policy.hidden_size=32 \
                    --policy.num_layers=2 --policy.transformer_heads=2 \
                    > "$transformer_report/$transformer_id.exploit.txt"
                printf '%s\t%s\t%s\t%s\t%s\n' "$transformer_model" "$transformer_async" \
                    "$transformer_graphs" "$transformer_frozen" "$transformer_bytes"
            done
        done
    done
done
