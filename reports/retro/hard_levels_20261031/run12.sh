#!/usr/bin/env bash
# Run 12 (2026-11-01): ONE 100M test at frameskip 1 (one decision per NES frame; everything since 2026-09-27 ran at 4),
# fresh policy, 4-4 and 8-4 only, practice starts along the warpless TAS route (tas/tas_44_84.archive.gz).
# Evals read the checkpoint config, so they also play at frameskip 1. Waits for run 11 to finish first.
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
while tmux has-session -t run11 2>/dev/null; do sleep 20; done
L=4-4,8-4
KP=$R/tas/tas_44_84.archive.gz
D=$R/run12
mkdir -p $D
./puffer train --base.load_model_path=None --env.frameskip=1 --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 \
  --env.archive_practice=1 --env.archive_start=0.75 --env.archive_init_path=$KP --env.terminate_on_clear=1 \
  --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 --env.archive_practice_focus=0.5 --env.pipe_entry_reward=1 --env.coin_reward=0.25 \
  --train.total_timesteps=100000000 --base.checkpoint_dir=$D/checkpoints --base.log_dir=$D/logs > $D/train.log 2>&1
echo "$(date) exit $?" >> $D/train.log
ck=$(ls $D/checkpoints/retro/*/*.bin 2>/dev/null | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $D/logs/retro/*.ini | tail -1) $ck.ini
for l in ${L//,/ }; do
  echo "$l $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
    --metric perf 2>&1 | grep retro_panel | grep -o -E "clears=[0-9]+|progress=[0-9.]+|frameskip=[0-9]+" | tr "\n" " ")"
done > $R/eval_run12.txt
./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" > $R/keypoint_eval_run12.txt
echo RUN12_DONE >> $R/eval_run12.txt
