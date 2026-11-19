#!/usr/bin/env bash
# After sweep trial 27: stop the sweep, run the 3 teacher arms in a row (each scored beside the next by autoscore), then
# resume the sweep from its history.
S=/workspace/chess_sweep
until grep -q -E "^sweep (run|worker run)=27 " $S/sweep.log; do sleep 30; done
touch $S/STOP; tmux kill-session -t sweep; sleep 10
for p in $(pgrep -f "^./puffer_chess_teach2s"); do kill $p; done; sleep 5
echo "$(date -u) sweep stopped after trial 27" >> /workspace/runs/arms_done.txt
for arm in "drop80 1 0.8" "base30 0 0.3" "drop30 1 0.3"; do
  set -- $arm
  R=$1 SWING=$2 TP=$3 bash /workspace/chess_teach/drop.sh & tp=$!
  sleep 60
  nohup bash /workspace/chess_eval/autoscore.sh $1 puffer_chess_drop 1024 /workspace/env.sh > /workspace/runs/autoscore_$1.log 2>&1 &
  wait $tp
done
wait
rm -f $S/STOP; tmux new -d -s sweep "bash $S/sweep.sh"
echo "$(date -u) arms done, sweep resumed" >> /workspace/runs/arms_done.txt
