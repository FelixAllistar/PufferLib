#!/usr/bin/env bash
# Score the magnet copy of every finished sweep trial with the in-trial ladder protocol (600 games vs top-move Maia
# 1100 from the training opening bank, run from this dir), plus trial 0's policy as a protocol check (in-trial .746).
source /workspace/env.sh; cd /workspace/chess_sweep
export MAIA_LC0_PATH=/workspace/chess_eval/lc0bin MAIA_WEIGHTS_PATH=/workspace/chess_eval/lc0/maia-1100.pb.gz MAIA_NODES=1
ev() { OMP_NUM_THREADS=8 timeout 1800 /workspace/PufferLib/puffer_chess_teach2s eval $1 --headless --base.env_name=chess \
  --env.mode=4 --env.fen_curric_pct=1 --env.perf_book_weight=0 --env.num_agents=1 --env.num_bots=1 --selfplay.enabled=0 \
  --vec.num_policies=1 --vec.hist_policy_percent=0 --vec.total_agents=32 --vec.num_buffers=1 --vec.num_threads=8 \
  --base.eval_episodes=600 --policy.hidden_size=1024 2>&1 | grep -a CUDA_EVAL | grep -o -E "perf=[0-9.]+" | head -1; }
# Trials of this sweep = the ones with a finished log ini (the killed 256 attempt left only an empty _0000 dir).
for ini in $(ls logs/chess/sweep_*.ini | sort -t_ -k3); do
  b=$(basename $ini .ini); r=${b##*_}; d=checkpoints/chess/$b
  f=$(ls $d/*.bin.magnet 2>/dev/null | tail -1); [ -z "$f" ] && continue
  if [ "$r" = 0000 ] && ! grep -q "^0000 policy_check" rescore.txt 2>/dev/null; then echo "0000 policy_check $(ev ${f%.magnet})" >> rescore.txt; fi
  grep -q "^$r magnet" rescore.txt 2>/dev/null && continue
  echo "$r magnet $(ev $f)" >> rescore.txt
done
echo "rescore done" >> rescore.txt
