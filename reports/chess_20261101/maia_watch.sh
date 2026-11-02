#!/usr/bin/env bash
# Chess baseline (2026-11-01): score each new checkpoint against Maia-1100 (lc0, 1 node, human-like ~1100 play),
# 200+ games, one eval at a time beside training. Appends "checkpoint score draw games" to maia_evals.txt.
cd /workspace/PufferLib
D=reports/chess_20261101
touch $D/maia_evals.txt
while true; do
  for ck in $(ls $D/checkpoints/chess/*/*.bin 2>/dev/null | grep -v magnet | sort); do
    grep -q "$ck" $D/maia_evals.txt && continue
    r=$(MAIA_LC0_PATH=./lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1100.pb.gz MAIA_NODES=1 OMP_NUM_THREADS=4 timeout 1200 \
      ./puffer_chess eval $ck --headless --base.env_name=chess --env.mode=4 --env.fen_curric_pct=0 --selfplay.enabled=0 \
      --vec.num_policies=1 --vec.total_agents=32 --vec.num_buffers=1 --vec.num_threads=4 --base.eval_episodes=200 2>&1 \
      | grep -a CUDA_EVAL | grep -o -E "score=[0-9.]+|games=[0-9]+|draw=[0-9.]+" | tr "\n" " ")
    echo "$ck $r" >> $D/maia_evals.txt
  done
  tmux has-session -t chess 2>/dev/null || break
  sleep 120
done
