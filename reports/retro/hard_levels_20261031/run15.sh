#!/usr/bin/env bash
# Run 15 (2026-11-01): run 14 (TAS clone + 400M RL) trained only on the spots it still fails, no more cloning:
# small-Mario TAS starts from key points 4 (4-4 Bowser bridge), 6-8 (8-4 piranha pipe and after) and 11 (8-4
# underwater), tas/tas_hard_small.archive.gz, 90% of episodes from those starts. Frameskip 1; 100M chunks, eval after
# each against ALL TAS sections and the real levels (eval_run15_N.txt). CHUNKS (default 2).
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
L=4-4,8-4
KP=$R/tas/tas_hard_small.archive.gz
ALLKP=$R/tas/tas_44_84_small.archive.gz
prev=$(ls $R/run14/chunk4/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
for n in $(seq 1 ${CHUNKS:-2}); do
  D=$R/run15/chunk$n
  mkdir -p $D
  ./puffer train --base.load_model_path=$prev --env.frameskip=1 --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 \
    --env.archive_practice=1 --env.archive_start=0.9 --env.archive_init_path=$KP --env.terminate_on_clear=1 \
    --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 --env.archive_practice_focus=0.5 --env.pipe_entry_reward=1 --env.coin_reward=0.25 \
    --train.total_timesteps=100000000 --base.checkpoint_dir=$D/checkpoints --base.log_dir=$D/logs > $D/train.log 2>&1
  ck=$(ls $D/checkpoints/retro/*/*.bin 2>/dev/null | grep -v -E "magnet|archive" | sort | tail -1)
  [ -n "$ck" ] || break
  cp $(ls $D/logs/retro/*.ini | tail -1) $ck.ini
  for l in ${L//,/ }; do
    echo "$l $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
      --metric perf 2>&1 | grep retro_panel | grep -o -E "clears=[0-9]+|progress=[0-9.]+" | tr "\n" " ")"
  done > $R/eval_run15_$n.txt
  ./build/retro/keypoint_eval $ck $ALLKP 8 300 2>&1 | grep -v "^\[retro\]" | tail -14 | tr "\n" ";" >> $R/eval_run15_$n.txt
  prev=$ck
done
echo RUN15_DONE >> $R/eval_run15_$n.txt
