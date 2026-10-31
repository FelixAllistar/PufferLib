#!/usr/bin/env bash
# Run 7 (2026-10-31): keep the box training ~12 h from run 6 in 500M chunks (each one loads the previous chunk's last
# checkpoint), with the real-game and key-point evals after every chunk (eval_run7_<n>.txt). Chunks because the host
# went from 183K to ~13K SPS (CPU pressure from a neighbour); a fixed 7B run would never reach its eval.
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
L=4-4,7-4,8-4,5-3,8-1,8-3
KP=$R/kp_g.archive.gz
prev=$(ls $R/run6/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
end=$(( $(date +%s) + 12 * 3600 ))
n=1
while [ "$(date +%s)" -lt "$end" ]; do
  D=$R/run7/chunk$n
  mkdir -p $D
  ./puffer train --base.load_model_path=$prev --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 \
    --env.archive_practice=1 --env.archive_start=0.75 --env.archive_init_path=$KP --env.terminate_on_clear=1 \
    --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 --env.archive_practice_focus=0.5 \
    --train.total_timesteps=500000000 --base.checkpoint_dir=$D/checkpoints --base.log_dir=$D/logs > $D/train.log 2>&1
  echo "$(date) exit $?" >> $D/train.log
  ck=$(ls $D/checkpoints/retro/*/*.bin 2>/dev/null | grep -v -E "magnet|archive" | sort | tail -1)
  [ -n "$ck" ] || break
  cp $(ls $D/logs/retro/*.ini | tail -1) $ck.ini
  for l in ${L//,/ }; do
    echo "$l $ck $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
      --metric perf 2>&1 | grep retro_panel | grep -o -E 'clears=[0-9]+|progress=[0-9.]+' | tr '\n' ' ')"
  done > $R/eval_run7_$n.txt
  ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" > $R/keypoint_eval_run7_$n.txt
  prev=$ck
  n=$((n + 1))
done
