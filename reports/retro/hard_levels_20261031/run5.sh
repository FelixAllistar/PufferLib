#!/usr/bin/env bash
# Run 5 (2026-10-31): run 4 continued 500M more, same bank and pipe reward, plus env.archive_practice_focus=0.5: half
# of the key-point starts go to points by failure rate (8-4 pipe, 4-4 loop 2 were 0% and 59% after run 4).
cd /workspace/PufferLib
R=reports/retro/hard_levels_20261031
L=4-4,7-4,8-4,5-3,8-1,8-3
KP=$R/kp_f.archive.gz
mkdir -p $R/run5
./puffer train --base.load_model_path=$(ls $R/run4/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1) \
  --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 --env.archive_practice=1 --env.archive_start=0.75 \
  --env.archive_init_path=$KP --env.terminate_on_clear=1 --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 --env.archive_practice_focus=0.5 \
  --train.total_timesteps=500000000 --base.checkpoint_dir=$R/run5/checkpoints --base.log_dir=$R/run5/logs \
  > $R/run5/train.log 2>&1
echo "$(date) exit $?" >> $R/run5/train.log
ck=$(ls $R/run5/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $R/run5/logs/retro/*.ini | tail -1) $ck.ini
for l in ${L//,/ }; do
  echo "$l $ck $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
    --metric perf 2>&1 | grep retro_panel | grep -o -E 'clears=[0-9]+|progress=[0-9.]+' | tr '\n' ' ')"
done > $R/eval_run5.txt
OMP_NUM_THREADS=22 ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" \
  > $R/keypoint_eval_run5.txt
echo EVAL_DONE >> $R/eval_run5.txt
