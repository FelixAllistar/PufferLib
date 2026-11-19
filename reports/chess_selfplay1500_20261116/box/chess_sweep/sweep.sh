#!/usr/bin/env bash
# Chess hyper sweep (config/chess.ini here; copy of reports/chess_selfplay1500_20261116/sweep256_chess.ini).
source /workspace/env.sh; cd /workspace/chess_sweep
unset CHESS_TEACHER_STATE
[ "${SWING:-0}" = 1 ] && export CHESS_TEACH_SWING=1
export MAIA_LC0_PATH=/workspace/chess_eval/lc0bin MAIA_WEIGHTS_PATH=/workspace/chess_eval/lc0/maia-1100.pb.gz MAIA_NODES=1
export OMP_NUM_THREADS=26
./puffer_chess_teach2s sweep --headless --base.env_name=chess --base.checkpoint_dir=/workspace/chess_sweep/checkpoints \
  --base.log_dir=/workspace/chess_sweep/logs >> /workspace/chess_sweep/sweep.log 2>&1
echo "$(date -u) sweep exit $?" >> /workspace/runs/arms_done.txt
