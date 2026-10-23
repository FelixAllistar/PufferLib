#!/usr/bin/env bash
# Magnet KL (EMAgnet) checks on a short GPU Goofspiel run. Needs a goofspiel --cu build.
# 1. magnet_coef=0 trains bit-identically to a run without any magnet settings.
# 2. magnet_tau=0 keeps the magnet at the learner's initial weights.
# 3. magnet_tau=1 makes the magnet equal the learner after the final pass.
# 4. magnet_reverse=1 trains and keeps the same EMA (tau=1 still tracks the learner).
# 5. magnet_path=random starts the magnet from fresh weights, not the learner's.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# != 1 || -e $1 ]]; then
    printf 'Usage: %s NEW_REPORT_DIRECTORY\n' "$0" >&2
    exit 1
fi
out=$1
bin=${GOOFSPIEL_TRAIN_BINARY:-./puffer}
mkdir -p "$out"
run() {
    local name=$1
    shift
    "$bin" train --base.checkpoint_dir="$out/$name" --base.log_dir="$out/$name/logs" \
        --base.seed=73 --base.async=0 --base.checkpoint_interval=0 --vec.num_buffers=1 \
        --vec.total_agents=4096 --train.total_timesteps=2097152 --train.replay_ratio=4 \
        "$@" > "$out/$name.log" 2>&1
    final=$(ls -t $(find "$out/$name" -name '*.bin') | head -1)
    initial=$(find "$out/$name" -name '0000000000000000.bin')
}
sha() { sha256sum "$1" | cut -d' ' -f1; }

run plain
plain=$(sha "$final")
run off --train.magnet_coef=0
[[ $(sha "$final") == "$plain" ]] || { echo 'FAIL: magnet_coef=0 changed training'; exit 1; }
run frozen --train.magnet_coef=1 --train.magnet_tau=0
[[ $(sha "$final.magnet") == $(sha "$initial") ]] || { echo 'FAIL: tau=0 moved magnet'; exit 1; }
run follow --train.magnet_coef=1 --train.magnet_tau=1
[[ $(sha "$final.magnet") == $(sha "$final") ]] || { echo 'FAIL: tau=1 magnet != learner'; exit 1; }
run reverse --train.magnet_coef=1 --train.magnet_tau=1 --train.magnet_reverse=1
[[ $(sha "$final.magnet") == $(sha "$final") ]] || { echo 'FAIL: reverse tau=1 magnet != learner'; exit 1; }
run random --train.magnet_coef=1 --train.magnet_tau=0 --base.magnet_path=random
[[ $(sha "$final.magnet") != $(sha "$initial") ]] || { echo 'FAIL: random magnet copied learner'; exit 1; }
echo 'PASS: magnet off is identical, tau=0 holds the anchor, tau=1 tracks the learner,' \
    'reverse KL trains, random magnet is fresh'
