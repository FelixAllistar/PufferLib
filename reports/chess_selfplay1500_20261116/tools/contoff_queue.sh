#!/usr/bin/env bash
# 2026-11-19 control for cont_drop30 (.681/.681 vs Maia 1900, main2 .659/.640): the identical main2 continuation
# (300M, lr 0.0008, ent 0.00058, seed 73) with the teacher OFF (TP 0). Then both finals vs Maia 1900. After harvest.sh.
while tmux has-session -t harvest 2>/dev/null; do sleep 30; done
S=/workspace/chess_sweep
n=$(grep -c -E "^sweep (run|worker run)=" $S/sweep.log)
until [ $(grep -c -E "^sweep (run|worker run)=" $S/sweep.log) -gt $n ]; do sleep 20; done
touch $S/STOP; tmux kill-session -t sweep; sleep 10
for p in $(pgrep -f "^./puffer_chess_teach2s"); do kill $p; done; sleep 5
echo "$(date -u) sweep paused for cont_off" >> /workspace/runs/arms_done.txt
cd /workspace/chess_teach
CHESS_TEACH_SWING=0 H=1024 R=cont_off BIN=puffer_chess_drop TP=0 STEPS=300000000 LR=0.0008 ENT=0.00058 ENTA=0 CKI=381 \
  SEED=73 THREADS=26 START=/workspace/main2_ckpt/0000002999975936.bin bash bench.sh
source /workspace/env.sh; cd /workspace/chess_eval
f=$(ls /workspace/runs/cont_off/checkpoints/chess/cont_off/*.bin | tail -1)
for m in "" .magnet; do
  r=$(MAIA_LC0_PATH=$PWD/lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1900.pb.gz MAIA_NODES=1 OMP_NUM_THREADS=8 timeout 1800 \
      /workspace/PufferLib/puffer_chess_drop eval $f$m --headless --base.env_name=chess --env.mode=4 --env.fen_curric_pct=1 \
      --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=32 --vec.num_buffers=1 --vec.num_threads=8 \
      --base.eval_episodes=1000 --policy.hidden_size=1024 2>&1 | grep -a CUDA_EVAL | grep -o -E "perf=[0-9.]+|draw=[0-9.]+" | tr "\n" " ")
  echo "cont_off${m:+_magnet} maia1900-top $r (1000 games) $(date -u +%H:%M)" | tee -a results.txt teach1900.txt
done
rm -f $S/STOP; tmux new -d -s sweep "bash $S/sweep.sh"
echo "$(date -u) cont_off done, sweep resumed" >> /workspace/runs/arms_done.txt
