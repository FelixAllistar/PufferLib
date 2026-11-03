#!/usr/bin/env bash
# Opening watcher (2026-11-03): each new checkpoint in DIR plays 100 games against the random mover from the normal
# start with PGN logging; opening_report.py's first-move and book lines go to DIR/openings.txt.
# Usage: opening_watch.sh DIR TRAIN_TMUX_SESSION BINARY
cd /workspace/PufferLib
D=$1; S=$2; B=$3
touch $D/openings.txt
while true; do
  for ck in $(ls $D/checkpoints/chess/*/*.bin 2>/dev/null | grep -v magnet | sort); do
    grep -q "$ck" $D/openings.txt && continue
    P=$D/pgn_$(basename $ck .bin); rm -f $P.*.pgn
    CHESS_PGN_OUT=$P OMP_NUM_THREADS=8 timeout 1200 $B eval $ck --headless --base.env_name=chess --env.mode=0 \
      --env.fen_curric_pct=0 --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=64 --vec.num_buffers=1 \
      --vec.num_threads=8 --base.eval_episodes=100 > /dev/null 2>&1
    echo "== $ck" >> $D/openings.txt
    /workspace/pyenv/bin/python ocean/chess/tools/opening_report.py "$P.*.pgn" | grep -E "first moves|book move" >> $D/openings.txt
  done
  tmux has-session -t =$S 2>/dev/null || break
  sleep 120
done
echo WATCH_DONE >> $D/openings.txt
