#!/usr/bin/env bash
# Run 16 (2026-11-01): RL fine-tune of bc/bc_sections.bin (run 14 + TAS cloning ONLY in the 4-4 maze, frames 0-960,
# and the 8-4 pipe route and underwater, frames 0-1680; KL to run 14 everywhere else). Frameskip 1, small TAS
# starts; 100M chunks with an eval (real levels + route-based section test) after each. CHUNKS (default 2).
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
L=4-4,8-4
KP=$R/tas/tas_44_84_small.archive.gz
prev=$R/bc/bc_sections.bin
for n in $(seq 1 ${CHUNKS:-2}); do
  D=$R/run16/chunk$n
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
  done > $R/eval_run16_$n.txt
  KP_ROUTE=1 ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" | tail -14 | tr "\n" ";" >> $R/eval_run16_$n.txt
  prev=$ck
done
echo RUN16_DONE >> $R/eval_run16_$n.txt
