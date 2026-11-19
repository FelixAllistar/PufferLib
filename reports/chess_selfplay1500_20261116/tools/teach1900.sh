#!/usr/bin/env bash
# 2026-11-19: does the teacher show up vs a stronger scorer? cont_drop30 final (main2 + 300M drop ladder) and the
# 400M scratch teacher runs vs top-move Maia 1900, 1000 games, policy + magnet. main2 itself: .659 / .640.
# Runs between sweep trials (pause, eval, resume) as ladder_gap.sh does.
S=/workspace/chess_sweep
n=$(grep -c -E "^sweep (run|worker run)=" $S/sweep.log)
until [ $(grep -c -E "^sweep (run|worker run)=" $S/sweep.log) -gt $n ]; do sleep 20; done
touch $S/STOP; tmux kill-session -t sweep; sleep 10
for p in $(pgrep -f "^./puffer_chess_teach2s"); do kill $p; done; sleep 5
echo "$(date -u) sweep paused for teacher-vs-Maia1900 evals" >> /workspace/runs/arms_done.txt
source /workspace/env.sh; cd /workspace/chess_eval
for ck in cont_drop30/0000000299892736 base30/0000000399769600 drop30/0000000399769600; do for m in "" .magnet; do
  f=/workspace/runs/${ck%%/*}/checkpoints/chess/$ck.bin$m
  r=$(MAIA_LC0_PATH=$PWD/lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1900.pb.gz MAIA_NODES=1 OMP_NUM_THREADS=8 timeout 1800 \
      /workspace/PufferLib/puffer_chess_drop eval $f --headless --base.env_name=chess --env.mode=4 --env.fen_curric_pct=1 \
      --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=32 --vec.num_buffers=1 --vec.num_threads=8 \
      --base.eval_episodes=1000 --policy.hidden_size=1024 2>&1 | grep -a CUDA_EVAL | grep -o -E "perf=[0-9.]+|draw=[0-9.]+" | tr "\n" " ")
  echo "${ck%%/*}${m:+_magnet} maia1900-top $r (1000 games) $(date -u +%H:%M)" | tee -a results.txt teach1900.txt
done; done
rm -f $S/STOP; tmux new -d -s sweep "bash $S/sweep.sh"
echo "$(date -u) teacher evals done, sweep resumed" >> /workspace/runs/arms_done.txt
