#!/usr/bin/env bash
# Negative integration checks; no malformed case may start training successfully.
set -euo pipefail
cd "$(dirname "$0")/.."
transformer_binary=${GOOFSPIEL_TRAIN_BINARY:-./build/puffer_goofspiel_transformer}
if [[ $# != 2 || -e $1 || ! -f $2 ]]; then
    printf 'Usage: %s NEW_REPORT_DIRECTORY TRANSFORMER_CHECKPOINT\n' "$0" >&2
    exit 1
fi
transformer_report=$1
transformer_checkpoint=$2
mkdir -p "$transformer_report"
transformer_common=(
    --base.checkpoint_dir="$transformer_report/checkpoints" --base.log_dir="$transformer_report/logs"
    --base.eval_episodes=0 --base.checkpoint_interval=0 --base.async=1
    --vec.total_agents=64 --vec.num_buffers=2 --vec.num_threads=2 --selfplay.enabled=0
    --policy.architecture=transformer --policy.hidden_size=32 --policy.num_layers=2
    --train.horizon=8 --train.minibatch_size=128 --train.total_timesteps=512
)
for transformer_bad in env.open_spiel_obs=1 env.information=1 env.egocentric=0 \
        env.num_players=3 policy.hidden_size=12 policy.transformer_heads=3 \
        policy.num_layers=0 policy.architecture=unknown; do
    transformer_id=${transformer_bad//[.=]/_}
    if "$transformer_binary" train "${transformer_common[@]}" --base.run_id="$transformer_id" \
            "--$transformer_bad" > "$transformer_report/$transformer_id.log" 2>&1; then
        printf 'Invalid configuration accepted: %s\n' "$transformer_bad" >&2
        exit 1
    fi
    case "$transformer_bad" in
        env.*) transformer_reason='requires compact' ;;
        policy.architecture=*) transformer_reason='unknown policy.architecture' ;;
        policy.transformer_heads=*) transformer_reason='must divide' ;;
        policy.num_layers=0) transformer_reason='positive integers' ;;
        *) transformer_reason='requires width' ;;
    esac
    grep -Fq "$transformer_reason" "$transformer_report/$transformer_id.log"
    printf 'Rejected %s\n' "$transformer_bad"
done
if "$transformer_binary" train "${transformer_common[@]}" --base.run_id=wrong_checkpoint \
        --policy.architecture=puffernet --base.load_model_path="$transformer_checkpoint" \
        > "$transformer_report/wrong_checkpoint.log" 2>&1; then
    printf 'PufferNet accepted a Transformer checkpoint\n' >&2
    exit 1
fi
grep -Fq 'Goofspiel checkpoint shape mismatch' "$transformer_report/wrong_checkpoint.log"
printf 'Rejected Transformer checkpoint under PufferNet architecture\n'
