#!/usr/bin/env bash
# After main2 ends and autoscore is done: Stockfish 1320 at 1 s/move, 200 games, quiet box, on the final magnet + policy.
source /workspace/env.sh; cd /workspace/chess_eval
until grep -q "^done" curve_main2.txt 2>/dev/null; do sleep 60; done
ln -sfn /workspace/PufferLib/puffer_chess_teach2 puffer_chess  # main2 = FEATS2 build
C=$(ls /workspace/runs/main2/checkpoints/chess/main2/0*.bin | sort | tail -n 1); s=$((10#$(basename $C .bin)))
for ck in $C.magnet $C; do tag=main2_$s; [[ $ck == *.magnet ]] && tag=${tag}_magnet
  SF_GAMES=200 SF_LEVELS=1320 SF_AGENTS=11 SF_THREADS=11 timeout 5400 bash run_eval.sh $ck ${tag}_sf_quiet sf
done
ln -sfn /workspace/PufferLib/puffer_chess_teach puffer_chess
echo "$(date -u) SF main2 DONE" >> /workspace/runs/arms_done.txt
