#!/usr/bin/env bash
# Hard levels from scratch (2026-10-31): a fresh policy (fresh_1b's tuned config, replay_ratio 1) on only 4-4, 7-4,
# 8-4, 5-3, 8-1, 8-3. 25% of episodes start at a level start and play the real game until the first lost life; 75%
# start at one of 20 key points (keypoints.txt, states from keypoint_states with fresh_1b), every point equally often,
# where a maze loop-back counts as a death. 100M steps, then the real-game eval: each level from its start, 32 tries.
cd /workspace/PufferLib
R=reports/retro/hard_levels_20261031
L=4-4,7-4,8-4,5-3,8-1,8-3
./puffer train --base.load_model_path=None --vec.num_threads=22 --env.spawn_levels=$L \
  --env.archive_enabled=1 --env.archive_practice=1 --env.archive_start=0.75 \
  --env.archive_init_path=$R/keypoints.archive.gz --env.terminate_on_clear=1 --env.maze_loop_terminal=0 \
  --train.total_timesteps=100000000 --base.checkpoint_dir=$R/run/checkpoints --base.log_dir=$R/run/logs \
  > $R/run/train.log 2>&1
echo "$(date) exit $?" >> $R/run/train.log
ck=$(ls $R/run/checkpoints/retro/*/*.bin | grep -v -E "magnet|archive" | sort | tail -1)
cp $(ls $R/run/logs/retro/*.ini | tail -1) $ck.ini
for c in saved/retro/watch_20261021/fresh_1b.bin $ck; do
  for l in ${L//,/ }; do
    echo "$l $c $(./build/retro/sweep_eval $c --config $c.ini --levels $l --frames 18000 --repeats 32 --workers 22 \
      --metric perf 2>&1 | grep retro_panel | grep -o -E 'clears=[0-9]+|progress=[0-9.]+' | tr '\n' ' ')"
  done
done > $R/eval.txt
echo EVAL_DONE >> $R/eval.txt
