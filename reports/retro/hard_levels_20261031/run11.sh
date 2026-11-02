#!/usr/bin/env bash
# Run 11 (2026-11-01): ONE 100M test. Run 10 chunk 21 continued on 4-4 and 8-4 with practice starts along the
# published warpless TAS route (tas/tas_44_84.archive.gz, tas_states: a start every 8 TAS frames on the ground,
# key point per 240 frames) instead of the searched key points. Then: real-game evals and the TAS-start eval.
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
L=4-4,8-4
KP=$R/tas/tas_44_84.archive.gz
prev=$(ls $R/run10/chunk21/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | tail -1)
D=$R/run11
mkdir -p $D
./puffer train --base.load_model_path=$prev --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 \
  --env.archive_practice=1 --env.archive_start=0.75 --env.archive_init_path=$KP --env.terminate_on_clear=1 \
  --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 --env.archive_practice_focus=0.5 --env.pipe_entry_reward=1 --env.coin_reward=0.25 \
  --train.total_timesteps=100000000 --base.checkpoint_dir=$D/checkpoints --base.log_dir=$D/logs > $D/train.log 2>&1
echo "$(date) exit $?" >> $D/train.log
ck=$(ls $D/checkpoints/retro/*/*.bin 2>/dev/null | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $D/logs/retro/*.ini | tail -1) $ck.ini
for l in ${L//,/ }; do
  echo "$l $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
    --metric perf 2>&1 | grep retro_panel | grep -o -E "clears=[0-9]+|progress=[0-9.]+" | tr "\n" " ")"
done > $R/eval_run11.txt
./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" > $R/keypoint_eval_run11.txt
echo RUN11_DONE >> $R/eval_run11.txt
