#!/usr/bin/env bash
# Run 17c (2026-11-02): from chunk 5 with ent_coef 0.003 (entropy was 0.15: the big model barely explores). Run 17b: run 17 from chunk 2, plus pipe_entry_reward 1 and practice focus 0.8 (8-4 deaths: jumps into the piranha plant; stuck underwater at x 162). Run 17: continue the big all-level model (saved/retro/big/restart_best_t12.bin, 191/256 panel) with its
# own settings (frameskip 4, full game from random levels, 30-minute episodes, archive shape; replay_ratio 1, lr 5e-5)
# plus practice starts at the spots that block 4-4 and 8-4: tas/mix17_small.archive.gz (small Mario; 4-4 final maze
# loop = key points 2-3; 8-4 key point 6 standing still before the lava jump so the run-up must be learned, 7-11 =
# piranha pipe, pipe route, underwater). archive_start 0.35 (35% of episodes from those starts, half of the picks
# weighted by failure), the rest full-game runs. No cloning (a section clone cost 40 of 128 all-level clears).
# 100M chunks; after each: full 4-4 / 8-4 (32 tries), the 32-level panel (4 tries each), route section test.
cd /workspace/PufferLib
export OMP_NUM_THREADS=22
R=reports/retro/hard_levels_20261031
KP=$R/tas/mix17_small.archive.gz
prev=$(ls $R/run17/chunk5/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
for n in $(seq 6 ${CHUNKS:-11}); do
  D=$R/run17/chunk$n
  mkdir -p $D
  ./puffer train --base.load_model_path=$prev --vec.num_threads=22 --env.spawn_levels=all --env.terminate_on_clear=0 \
    --env.max_frames=108000 --env.archive_enabled=1 --env.archive_practice=1 --env.archive_start=0.35 \
    --env.archive_practice_focus=0.8 --env.pipe_entry_reward=1 --env.archive_init_path=$KP --env.archive_capacity_mb=1024 \
    --env.archive_anneal_fraction=0.477510065 --env.archive_capture_interval=256 --env.archive_cell_width=128 \
    --env.archive_depth_priority=0.888039112 --env.archive_end=0.132962301 --env.archive_level_focus=0.15979284 \
    --env.archive_priority=0.865711808 --env.maze_loop_penalty=0.295226157 --env.maze_loop_terminal=0 \
    --train.anneal_lr=0 --train.ent_coef=0.003 --train.learning_rate=5e-05 --train.replay_ratio=1 \
    --train.total_timesteps=100000000 --base.checkpoint_dir=$D/checkpoints --base.log_dir=$D/logs > $D/train.log 2>&1
  ck=$(ls $D/checkpoints/retro/*/*.bin 2>/dev/null | grep -v -E "magnet|archive" | sort | tail -1)
  [ -n "$ck" ] || break
  cp $(ls $D/logs/retro/*.ini | tail -1) $ck.ini
  {
  for l in 4-4 8-4; do
    echo "$l $(./build/retro/sweep_eval $ck --config $ck.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
      --metric perf 2>&1 | grep retro_panel | grep -o -E "clears=[0-9]+|progress=[0-9.]+" | tr "\n" " ")"
  done
  echo "all $(./build/retro/sweep_eval $ck --config $ck.ini --levels all --frames 18000 --repeats 4 --workers 22 \
      --metric perf 2>&1 | grep retro_panel | grep -o -E "clears=[0-9]+|attempts=[0-9]+" | tr "\n" " ")"
  KP_ROUTE=1 ./build/retro/keypoint_eval $ck $R/tas/tas_44_84_small.archive.gz 8 300 2>&1 | grep -E "^key point|mean" \
    | sed -E "s/key point ([0-9]+): .* = /\1:/; s/mean over key points/mean/" | tr "\n" " "
  } > $R/eval_run17_$n.txt
  prev=$ck
done
echo RUN17C_DONE >> $R/eval_run17_$n.txt
