#!/usr/bin/env bash
# main1 (2026-11-17): pure self-play from scratch, 1024x2, per-move lookahead features (fixed layout) + state-group
# teacher 0.3. One long run: 6B steps, lr 0.00208645 -> 0.4x, entropy 0.0058 -> 0.1x, magnet 0.003 / tau 0.005 (the
# run's own EMA), 2048 agents, horizon 128, minibatch 8192, fen_curric_pct 0.9 (states_100k). Paused every ~1.05B
# for a score of policy and magnet: top-move Maia 1100 (200 games) and Stockfish 1320 at 1 s/move (100 games; 200 at
# the end). Progress lines: /workspace/runs/main1_done.txt; scores: /workspace/chess_eval/results.txt (tags main1_*).
source /workspace/env.sh; cd /workspace/chess_teach
R=main1; A=/workspace/runs; CK=$A/$R/checkpoints/chess/$R; LOG=$A/$R/train.log; D=$A/main1_done.txt; mkdir -p $A/$R
ev() {  # checkpoint tag sf_games
  (cd /workspace/chess_eval && ln -sfn /workspace/PufferLib/puffer_chess_teach puffer_chess
   MAIA_GAMES=200 timeout 1800 bash run_eval.sh $1 $2 maia | head -n 1
   SF_GAMES=$3 SF_LEVELS=1320 SF_AGENTS=11 SF_THREADS=11 timeout 3600 bash run_eval.sh $1 $2 sf) >> $A/$R/evals.log 2>&1
}
last() { ls $CK/0*.bin 2>/dev/null | sort | tail -n 1; }
step() { local f=$(last); [ -n "$f" ] && echo $((10#$(basename $f .bin))) || echo 0; }
next=1049100288; res=0; [ -f $CK/state/state.txt ] && res=1
while true; do
  H=1024 R=$R TP=0.3 STEPS=6000000000 LR=0.00208645 CKI=1000 RES=$res ./bench.sh &
  T=$!; res=1
  while kill -0 $T 2>/dev/null && [ $(step) -lt $next ]; do sleep 60; done
  kill -0 $T 2>/dev/null && touch $CK/PAUSE
  wait $T
  F=$(last); S=$(step)
  [ -n "$F" ] || { echo "$(date -u) $R no checkpoint, stopped" >> $D; exit 1; }
  if grep -qa "Paused at" <(tail -c 4000 $LOG); then
    ev $F ${R}_$S 100; ev $F.magnet ${R}_${S}_magnet 100; next=$((S + 1049100288))
    echo "$(date -u) $R scored at $S" >> $D
  else
    ev $F ${R}_final 200; ev $F.magnet ${R}_final_magnet 200; echo "$(date -u) $R final at $S" >> $D; break
  fi
done
