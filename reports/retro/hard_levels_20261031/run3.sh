#!/usr/bin/env bash
# Run 3 (2026-10-31): run 2 (500M) continued 500M more with the full key-point bank: the 20 first key points plus
# the routes found by keypoint_search past every maze wall (7-4 both groups, 4-4 loop 2, all 8-4 pipes; kp_f).
cd /workspace/PufferLib
until grep -q EVAL_DONE reports/retro/hard_levels_20261031/eval_run2.txt 2>/dev/null; do sleep 30; done
R=reports/retro/hard_levels_20261031
L=4-4,7-4,8-4,5-3,8-1,8-3
KP=$R/kp_f.archive.gz
mkdir -p $R/run3
./puffer train --base.load_model_path=$(ls $R/run2/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1) \
  --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 --env.archive_practice=1 --env.archive_start=0.75 \
  --env.archive_init_path=$KP --env.terminate_on_clear=1 --env.maze_loop_terminal=0 \
  --train.total_timesteps=500000000 --base.checkpoint_dir=$R/run3/checkpoints --base.log_dir=$R/run3/logs \
  > $R/run3/train.log 2>&1
echo "$(date) exit $?" >> $R/run3/train.log
ck=$(ls $R/run3/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $R/run3/logs/retro/*.ini | tail -1) $ck.ini
for l in ${L//,/ }; do
  echo "$l $ck $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
    --metric perf 2>&1 | grep retro_panel | grep -o -E 'clears=[0-9]+|progress=[0-9.]+' | tr '\n' ' ')"
done > $R/eval_run3.txt
OMP_NUM_THREADS=22 ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" \
  > $R/keypoint_eval_run3.txt
echo EVAL_DONE >> $R/eval_run3.txt
