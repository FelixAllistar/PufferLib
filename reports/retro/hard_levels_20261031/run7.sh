#!/usr/bin/env bash
# Run 7 (2026-10-31): run 6 continued 7B steps (~10.5 h on the 5060), same settings and kp_g bank, to keep the box
# training overnight.
cd /workspace/PufferLib
R=reports/retro/hard_levels_20261031
L=4-4,7-4,8-4,5-3,8-1,8-3
KP=$R/kp_g.archive.gz
mkdir -p $R/run7
./puffer train --base.load_model_path=$(ls $R/run6/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1) \
  --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 --env.archive_practice=1 --env.archive_start=0.75 \
  --env.archive_init_path=$KP --env.terminate_on_clear=1 --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 --env.archive_practice_focus=0.5 \
  --train.total_timesteps=7000000000 --base.checkpoint_dir=$R/run7/checkpoints --base.log_dir=$R/run7/logs \
  > $R/run7/train.log 2>&1
echo "$(date) exit $?" >> $R/run7/train.log
ck=$(ls $R/run7/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $R/run7/logs/retro/*.ini | tail -1) $ck.ini
for l in ${L//,/ }; do
  echo "$l $ck $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
    --metric perf 2>&1 | grep retro_panel | grep -o -E 'clears=[0-9]+|progress=[0-9.]+' | tr '\n' ' ')"
done > $R/eval_run7.txt
OMP_NUM_THREADS=22 ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" \
  > $R/keypoint_eval_run7.txt
echo EVAL_DONE >> $R/eval_run7.txt
