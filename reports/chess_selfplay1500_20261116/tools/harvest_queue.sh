#!/usr/bin/env bash
# 2026-11-19 cont_harvest: = cont_off (main2 + 300M, lr 0.0008, ent 0.00058, seed 73) but teacher 0.3 with ONLY the
# real-opponent collapse banks (main2 vs Maia 1900, positions 2-16 plies before each collapse, "stm" scoring) as static
# groups (no endgame banks, no drop ladder). Bar: cont_off .681 / .674 vs Maia 1900.
S=/workspace/chess_sweep
n=$(grep -c -E "^sweep (run|worker run)=" $S/sweep.log)
until [ $(grep -c -E "^sweep (run|worker run)=" $S/sweep.log) -gt $n ]; do sleep 20; done
touch $S/STOP; tmux kill-session -t sweep; sleep 10
for p in $(pgrep -f "^./puffer_chess_teach2s"); do kill $p; done; sleep 5
echo "$(date -u) sweep paused for cont_harvest" >> /workspace/runs/arms_done.txt
cd /workspace/chess_teach
WD=/workspace/chess_harvest CHESS_TEACH_SWING=0 H=1024 R=cont_harvest BIN=puffer_chess_drop TP=0.3 STEPS=300000000 LR=0.0008 \
  ENT=0.00058 ENTA=0 CKI=381 SEED=73 THREADS=26 START=/workspace/main2_ckpt/0000002999975936.bin bash bench_wd.sh
source /workspace/env.sh; cd /workspace/chess_eval
f=$(ls /workspace/runs/cont_harvest/checkpoints/chess/cont_harvest/*.bin | tail -1)
for m in "" .magnet; do
  r=$(MAIA_LC0_PATH=$PWD/lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1900.pb.gz MAIA_NODES=1 OMP_NUM_THREADS=8 timeout 1800 \
      /workspace/PufferLib/puffer_chess_drop eval $f$m --headless --base.env_name=chess --env.mode=4 --env.fen_curric_pct=1 \
      --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=32 --vec.num_buffers=1 --vec.num_threads=8 \
      --base.eval_episodes=1000 --policy.hidden_size=1024 2>&1 | grep -a CUDA_EVAL | grep -o -E "perf=[0-9.]+|draw=[0-9.]+" | tr "\n" " ")
  echo "cont_harvest${m:+_magnet} maia1900-top $r (1000 games) $(date -u +%H:%M)" | tee -a results.txt teach1900.txt
done
rm -f $S/STOP; tmux new -d -s sweep "bash $S/sweep.sh"
echo "$(date -u) cont_harvest done, sweep resumed" >> /workspace/runs/arms_done.txt
