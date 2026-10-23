#!/usr/bin/env bash
# Cancel the sweep (user request 2026-10-23) once trial 2 has written its score.
cd /workspace/PufferLib/reports/pokemon_experiments/20261022_transformer_sweep
until [ "$(grep -c "^[0-9]" sweep.history)" -ge 3 ]; do sleep 30; done
pkill -STOP -P 31297; kill 31297; pkill -KILL -P 31297; sleep 5
echo "SWEEP CANCELLED after trial 2 at $(date -u)" | tee -a sweep.log
grep "^[0-9]" sweep.history
