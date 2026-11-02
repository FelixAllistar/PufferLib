#!/usr/bin/env bash
# Run 13 continued (2026-11-01): same settings, chunks 5..(4+CHUNKS) from the newest run 13 checkpoint, after the
# first four finish. Eval after every chunk (eval_run13_N.txt); stop it by hand when the evals flatten.
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
while tmux has-session -t =run13 2>/dev/null; do sleep 20; done
L=4-4,8-4
KP=$R/tas/tas_44_84_small.archive.gz
for n in $(seq 5 $((4 + ${CHUNKS:-8}))); do
  prev=$(ls $R/run13/chunk$((n - 1))/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
  D=$R/run13/chunk$n
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
  done > $R/eval_run13_$n.txt
  ./build/retro/keypoint_eval $ck $KP 8 300 2>&1 | grep -v "^\[retro\]" | tail -1 >> $R/eval_run13_$n.txt
done
echo RUN13_DONE >> $R/eval_run13_$n.txt
