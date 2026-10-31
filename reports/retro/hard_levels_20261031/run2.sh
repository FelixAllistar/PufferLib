#!/usr/bin/env bash
# Run 1 continued to 500M (2026-10-31): same settings, 400M more steps from its 100M checkpoint, then the same evals
# (real game from each level start; key-point pass rates).
cd /workspace/PufferLib
R=reports/retro/hard_levels_20261031
L=4-4,7-4,8-4,5-3,8-1,8-3
KP=${KP:-$R/keypoints.archive.gz}
mkdir -p $R/run2
./puffer train --base.load_model_path=$R/run/checkpoints/retro/1792615713897/0000000099876864.bin \
  --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 --env.archive_practice=1 --env.archive_start=0.75 \
  --env.archive_init_path=$KP --env.terminate_on_clear=1 --env.maze_loop_terminal=0 \
  --train.total_timesteps=400000000 --base.checkpoint_dir=$R/run2/checkpoints --base.log_dir=$R/run2/logs \
  > $R/run2/train.log 2>&1
echo "$(date) exit $?" >> $R/run2/train.log
ck=$(ls $R/run2/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $R/run2/logs/retro/*.ini | tail -1) $ck.ini
for l in ${L//,/ }; do
  echo "$l $ck $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
    --metric perf 2>&1 | grep retro_panel | grep -o -E 'clears=[0-9]+|progress=[0-9.]+' | tr '\n' ' ')"
done > $R/eval_run2.txt
OMP_NUM_THREADS=22 ./build/retro/keypoint_eval $ck $R/keypoints.archive.gz 8 300 2>&1 | grep -v "^\[retro\]" \
  > $R/keypoint_eval_run2.txt
echo EVAL_DONE >> $R/eval_run2.txt
