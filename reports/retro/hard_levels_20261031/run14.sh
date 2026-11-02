#!/usr/bin/env bash
# Run 14 (2026-11-01): RL fine-tune of the TAS behavior clone (bc/bc_run13.bin: run 13 chunk 7 trained 30 epochs on
# the warpless TAS's frames and buttons, bc_train.py) - the VPT recipe's last step. Frameskip 1, 4-4 and 8-4,
# small-Mario TAS-route starts; 100M chunks with an eval after each (eval_run14_N.txt). CHUNKS (default 4).
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
L=4-4,8-4
KP=$R/tas/tas_44_84_small.archive.gz
prev=$R/bc/bc_run13.bin
for n in $(seq 1 ${CHUNKS:-4}); do
  D=$R/run14/chunk$n
  mkdir -p $D
  ./puffer train --base.load_model_path=$prev --env.frameskip=1 --vec.num_threads=22 --env.spawn_levels=$L --env.archive_enabled=1 \
    --env.archive_practice=1 --env.archive_start=0.75 --env.archive_init_path=$KP --env.terminate_on_clear=1 \
    --env.maze_loop_terminal=0 --env.progress_gap_cap=2048 --env.archive_practice_focus=0.5 --env.pipe_entry_reward=1 --env.coin_reward=0.25 \
    --train.total_timesteps=100000000 --base.checkpoint_dir=$D/checkpoints --base.log_dir=$D/logs > $D/train.log 2>&1
  ck=$(ls $D/checkpoints/retro/*/*.bin 2>/dev/null | grep -v -E "magnet|archive" | sort | tail -1)
  [ -n "$ck" ] || break
  cp $(ls $D/logs/retro/*.ini | tail -1) $ck.ini
  for l in ${L//,/ }; do
    echo "$l $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
      --metric perf 2>&1 | grep retro_panel | grep -o -E "clears=[0-9]+|progress=[0-9.]+" | tr "\n" " ")"
  done > $R/eval_run14_$n.txt
  ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" | tail -1 >> $R/eval_run14_$n.txt
  prev=$ck
done
echo RUN14_DONE >> $R/eval_run14_$n.txt
