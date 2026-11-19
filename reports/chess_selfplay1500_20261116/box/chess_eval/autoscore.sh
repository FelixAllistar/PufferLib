#!/usr/bin/env bash
# Score every new checkpoint of a run beside its training (2026-11-17): Maia 1100, 1000 games, policy + magnet, one
# eval at a time, games logged for failure_scan.py. Usage: autoscore.sh RUN BINARY HIDDEN ENVFILE. Stops when the
# run's training process is gone and every checkpoint is scored. Output: /workspace/chess_eval/curve_RUN.txt
R=$1; BIN=$2; H=$3; source $4; cd /workspace/chess_eval; mkdir -p games
C=/workspace/runs/$R/checkpoints/chess/$R; OUT=curve_$R.txt; touch $OUT
while true; do
  live=$(pgrep -f "run_id=$R " > /dev/null && echo 1 || echo 0)
  for f in $(ls $C/0*.bin 2>/dev/null | sort); do
    s=$((10#$(basename $f .bin))); [ $s -eq 0 ] && continue
    [ -f $f.magnet ] || continue
    grep -q "^${R}_$s " $OUT && continue
    sleep 20  # let the writer finish
    for ck in $f $f.magnet; do tag=${R}_$s; [[ $ck == *.magnet ]] && tag=${tag}_magnet
      r=$(CHESS_GAMES_OUT=$PWD/games/$tag.txt MAIA_LC0_PATH=$PWD/lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1100.pb.gz MAIA_NODES=1 \
          OMP_NUM_THREADS=8 timeout 1800 /workspace/PufferLib/$BIN eval $ck --headless --base.env_name=chess --env.mode=4 \
          --env.fen_curric_pct=1 --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=32 --vec.num_buffers=1 \
          --vec.num_threads=8 --base.eval_episodes=1000 --policy.hidden_size=$H 2>&1 | grep -a CUDA_EVAL \
          | grep -o -E "perf=[0-9.]+|draw=[0-9.]+" | tr "\n" " ")
      echo "$tag maia1100-top $r (1000 games) $(date -u +%H:%M)" | tee -a $OUT results.txt
    done
  done
  [ $live = 0 ] && break
  sleep 120
done
echo "done $(date -u)" >> $OUT
