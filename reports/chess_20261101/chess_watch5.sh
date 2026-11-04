#!/usr/bin/env bash
# Chess eval watcher for several runs (2026-11-03): like chess_watch4.sh, but takes DIR:TMUX:BINARY triples and
# evaluates every new checkpoint of any of them, still one eval at a time. Per checkpoint: 500 games vs a random
# mover (PGNs -> DIR/openings.txt) and 200 vs Maia-1100 (DIR/evals.txt). Exits once every training session is gone.
# Win rate is CUDA_EVAL perf= (its score= is the sweep metric, e.g. book_moves since 2026-11-03).
# MAIA_GAMES (default 200) / RAND_GAMES (default 500) set the games per checkpoint: 200 Maia games swing about
# +-3 points, so use 600+ to see a trend.
# Usage: chess_watch5.sh DIR:TMUX:BINARY [DIR:TMUX:BINARY ...]
cd /workspace/PufferLib
ev() {  # binary checkpoint mode games agents
  OMP_NUM_THREADS=4 timeout 1200 $1 eval $2 --headless --base.env_name=chess --env.mode=$3 --env.fen_curric_pct=0 \
    --selfplay.enabled=0 --vec.num_policies=1 ${EVAL_EXTRA} --vec.total_agents=$5 --vec.num_buffers=1 --vec.num_threads=4 \
    --base.eval_episodes=$4 2>&1 | grep -a CUDA_EVAL | grep -o -E "perf=[0-9.]+|draw=[0-9.]+" | sed "s/perf=/score=/" | tr "\n" " "
}
for run in "$@"; do touch ${run%%:*}/evals.txt; done
while true; do
  alive=0
  for run in "$@"; do
    IFS=: read -r D S B <<< "$run"
    tmux has-session -t =$S 2>/dev/null && alive=1
    for ck in $(ls $D/checkpoints/chess/*/*.bin 2>/dev/null | grep -v magnet | sort); do
      grep -q "$ck" $D/evals.txt && continue
      P=$D/pgn_$(basename $ck .bin); rm -f $P.*.pgn
      r=$(CHESS_PGN_OUT=$P ev $B $ck 0 ${RAND_GAMES:-500} ${RAND_AGENTS:-256})
      { echo "== $ck"; /workspace/pyenv/bin/python ocean/chess/tools/opening_report.py "$P.*.pgn" | grep -E "first moves|book move"; } >> $D/openings.txt
      m=$(MAIA_LC0_PATH=./lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1100.pb.gz MAIA_NODES=1 ev $B $ck 4 ${MAIA_GAMES:-200} 32)
      echo "$ck random: $r maia1100: $m" >> $D/evals.txt
      break  # one checkpoint per run per pass, so the runs take turns
    done
  done
  [ $alive = 0 ] && break
  sleep 60
done
for run in "$@"; do echo WATCH_DONE >> ${run%%:*}/evals.txt; done
