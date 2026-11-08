#!/usr/bin/env bash
# Poker Kirby loop v1 driver (2026-11-08). Every 500M main steps (63K SPS: ~2.2 h, exploiter ~55 min): pause main (PAUSE -> state/), score main's policy and
# magnet on the fixed judge ([score_eval]: 8 frozen models + range/Fullhouse), train a main-initialized exploiter vs the
# frozen main, score it head to head (vs 5 copies of main; mirror = rake baseline) and on the judge, add it to
# main.league, resume main. One training run at a time. Results: results.txt.
cd /workspace/PufferLib; K=reports/poker_kirby1; R=$K/results.txt; D=$K/main/ck/poker/main
ev() { OMP_NUM_THREADS=8 ./puffer_poker_k1 eval --headless --base.env_name=poker --base.result_fd=1 --base.load_model_path=$1 "${@:2}" 2>/dev/null \
  | grep -a -o "score=[-0-9.e+]*" | head -1 | cut -d= -f2; }
step() { local f=$(ls $D/0*.bin 2>/dev/null | grep -v magnet | sort | tail -1); [ -n "$f" ] && echo $((10#$(basename $f .bin))) || echo 0; }
log() { echo "$(date -u +%m-%d_%H:%M) $*" | tee -a $R; }
N=1; next=500000000; args=()
while true; do
  log "main start ${args[*]}"
  bash $K/main_run.sh "${args[@]}" >> $K/main/train.log 2>&1 &
  T=$!
  while kill -0 $T 2>/dev/null && [ $(step) -lt $next ]; do sleep 60; done
  kill -0 $T 2>/dev/null && touch $D/PAUSE
  wait $T
  M=$(ls $D/0*.bin 2>/dev/null | grep -v magnet | sort | tail -1); S=$(step)
  [ -n "$M" ] || { log "main exited without a checkpoint (see main/train.log)"; break; }
  log "main $S judge policy=$(ev $M) magnet=$(ev $M.magnet)"
  if ! { [ -f $D/state/state.txt ] && [ $D/state/state.txt -nt $M ]; }; then
    [ $S -ge 2900000000 ] && log "main finished" || log "main stopped early at $S (see main/train.log)"; break
  fi
  cp $M $K/banks/main_$N.bin
  log "ex$N start vs main_$N"
  bash $K/ex_run.sh $N $K/banks/main_$N.bin > $K/ex$N.log 2>&1
  X=$(ls $K/ex$N/ck/poker/ex$N/0*.bin | grep -v magnet | sort | tail -1); cp $X $K/banks/ex$N.bin
  VS="--score_eval.native_league=$K/ex$N.league --score_eval.rules_fraction=0"
  log "ex$N vs main_$N: ex=$(ev $K/banks/ex$N.bin $VS) mirror=$(ev $K/banks/main_$N.bin $VS) | judge ex=$(ev $K/banks/ex$N.bin)"
  n=$(head -1 $K/main.league | awk '{print $5}'); { echo "poker_v1 6 1024 2 $((n + 1))"; tail -n +2 $K/main.league; echo "$K/banks/ex$N.bin"; } > $K/main.league.new
  mv $K/main.league.new $K/main.league
  args=(--base.resume=1); next=$((S + 500000000)); N=$((N + 1))
done
log "loop done"
