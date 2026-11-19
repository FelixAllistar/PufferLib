#!/usr/bin/env bash
# 2026-11-19: real-opponent failure states. main2 (policy + magnet) vs top-move Maia 1900, 2000 games each, every game
# logged (CHESS_GAMES_OUT: color, result, start fen, moves) for a Stockfish scan of where main2's position collapses.
# Runs at the sweep gap after teach1900.sh.
while tmux has-session -t teach1900 2>/dev/null; do sleep 30; done
S=/workspace/chess_sweep
n=$(grep -c -E "^sweep (run|worker run)=" $S/sweep.log)
until [ $(grep -c -E "^sweep (run|worker run)=" $S/sweep.log) -gt $n ]; do sleep 20; done
touch $S/STOP; tmux kill-session -t sweep; sleep 10
for p in $(pgrep -f "^./puffer_chess_teach2s"); do kill $p; done; sleep 5
echo "$(date -u) sweep paused for main2 game harvest" >> /workspace/runs/arms_done.txt
source /workspace/env.sh; cd /workspace/chess_eval; mkdir -p harvest
for m in "" .magnet; do
  CHESS_GAMES_OUT=/workspace/chess_eval/harvest/main2${m:+_magnet}_m1900.txt MAIA_LC0_PATH=$PWD/lc0bin \
  MAIA_WEIGHTS_PATH=lc0/maia-1900.pb.gz MAIA_NODES=1 OMP_NUM_THREADS=8 timeout 3000 \
      /workspace/PufferLib/puffer_chess_drop eval /workspace/main2_ckpt/0000002999975936.bin$m --headless --base.env_name=chess \
      --env.mode=4 --env.fen_curric_pct=1 --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=32 --vec.num_buffers=1 \
      --vec.num_threads=8 --base.eval_episodes=2000 --policy.hidden_size=1024 2>&1 | grep -a CUDA_EVAL | cut -c1-120 >> harvest/scores.txt
done
rm -f $S/STOP; tmux new -d -s sweep "bash $S/sweep.sh"
echo "$(date -u) harvest done, sweep resumed" >> /workspace/runs/arms_done.txt
