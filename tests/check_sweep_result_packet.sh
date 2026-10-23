#!/usr/bin/env bash
# Sweep parent vs trial result packets: a legacy 784-byte packet (the Python sweep workers) is
# accepted unchanged, and an extended packet reporting a different minibatch makes the sweep
# record the minibatch the trial actually used. Usage: tests/check_sweep_result_packet.sh BINARY
# (any env build, e.g. build/check_breakout). Runs from a scratch copy of the configs.
set -euo pipefail
bin=$(realpath "$1"); root=$(pwd); work=$(mktemp -d)
mkdir -p "$work/config"; cp config/default.ini config/breakout.ini "$work/config/"
printf '\n[sweep]\nworker_path = %s\n' "$root/tests/fake_sweep_worker.py" >> "$work/config/breakout.ini"
cd "$work"
for mode in legacy v2; do
  MODE=$mode "$bin" sweep --sweep.max_runs=3 --sweep.gpus=1 --sweep.downsample=1 \
    --sweep.history_path=$work/$mode.history --base.checkpoint_dir=$work/c --base.log_dir=$work/l > $mode.log 2>&1
  ! grep -q "marking sample bad" $mode.log
done
column=$(awk 'NR>1 && $1=="train.minibatch_size"{print NR-1; exit}' legacy.history)
legacy=$(awk -v n=$(head -1 legacy.history | cut -d" " -f2) -v c=$column 'NR>n+1 && NF>4{print $(4+c)}' legacy.history | tr '\n' ' ')
v2=$(awk -v n=$(head -1 v2.history | cut -d" " -f2) -v c=$column 'NR>n+1 && NF>4{print $(4+c)}' v2.history | tr '\n' ' ')
echo "legacy minibatch: $legacy"; echo "v2 minibatch (reported 1000, clamped to range min): $v2"
[ "$v2" = "$(echo "$v2" | tr ' ' '\n' | grep -v '^$' | sed 's/.*/4096/' | tr '\n' ' ')" ]
echo PASS sweep result packets
rm -rf "$work"
