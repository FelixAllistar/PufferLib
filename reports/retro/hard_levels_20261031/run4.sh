#!/usr/bin/env bash
# Run 4 (2026-10-31): run 3 continued 500M more, same bank (kp_f), plus env.progress_gap_cap=2048: a pipe warp
# forward inside an area (8-4) now pays its distance instead of the 64 px per-step cap.
cd /workspace/PufferLib
R=reports/retro/hard_levels_20261031
L=4-4,7-4,8-4,5-3,8-1,8-3
KP=$R/kp_f.archive.gz
mkdir -p $R/run4
./puffer train --base.load_model_path=$(ls $R/run3/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1) \
  --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 --env.archive_practice=1 --env.archive_start=0.75 \
  --env.archive_init_path=$KP --env.terminate_on_clear=1 --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 \
  --train.total_timesteps=500000000 --base.checkpoint_dir=$R/run4/checkpoints --base.log_dir=$R/run4/logs \
  > $R/run4/train.log 2>&1
echo "$(date) exit $?" >> $R/run4/train.log
ck=$(ls $R/run4/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $R/run4/logs/retro/*.ini | tail -1) $ck.ini
for l in ${L//,/ }; do
  echo "$l $ck $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
    --metric perf 2>&1 | grep retro_panel | grep -o -E 'clears=[0-9]+|progress=[0-9.]+' | tr '\n' ' ')"
done > $R/eval_run4.txt
OMP_NUM_THREADS=22 ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" \
  > $R/keypoint_eval_run4.txt
echo EVAL_DONE >> $R/eval_run4.txt
