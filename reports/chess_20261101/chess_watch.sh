#!/usr/bin/env bash
# Chess eval watcher (2026-11-01): each new checkpoint in DIR plays a random mover (500 games) and Maia-1100
# (lc0, 1 node, 200 games), one eval at a time. Appends "checkpoint random: ... maia1100: ..." to DIR/evals.txt.
# Usage: chess_watch.sh DIR TRAIN_TMUX_SESSION [BINARY=./puffer_chess]
cd /workspace/PufferLib
D=$1; S=$2; B=${3:-./puffer_chess}
touch $D/evals.txt
ev() {  # checkpoint mode games agents
  OMP_NUM_THREADS=4 timeout 1200 $B eval $1 --headless --base.env_name=chess --env.mode=$2 --env.fen_curric_pct=0 \
    --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=$4 --vec.num_buffers=1 --vec.num_threads=4 \
    --base.eval_episodes=$3 2>&1 | grep -a CUDA_EVAL | grep -o -E "score=[0-9.]+|draw=[0-9.]+" | tr "\n" " "
}
while true; do
  for ck in $(ls $D/checkpoints/chess/*/*.bin 2>/dev/null | grep -v magnet | sort); do
    grep -q "$ck" $D/evals.txt && continue
    r=$(ev $ck 0 500 256)
    m=$(MAIA_LC0_PATH=./lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1100.pb.gz MAIA_NODES=1 ev $ck 4 200 32)
    echo "$ck random: $r maia1100: $m" >> $D/evals.txt
  done
  tmux has-session -t $S 2>/dev/null || break
  sleep 60
done
echo WATCH_DONE >> $D/evals.txt
