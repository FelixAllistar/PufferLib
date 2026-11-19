#!/usr/bin/env bash
# Honest number for main1 (stopped at 5.24B): Stockfish 16 UCI_Elo 1320 at 1 s/move, 200 games, quiet box, magnet then policy.
source /workspace/env.sh; cd /workspace/chess_eval
C=/workspace/runs/main1/checkpoints/chess/main1/0000005243928576.bin
for ck in $C.magnet $C; do tag=main1_5243928576; [[ $ck == *.magnet ]] && tag=${tag}_magnet
  SF_GAMES=200 SF_LEVELS=1320 SF_AGENTS=11 SF_THREADS=11 timeout 5400 bash run_eval.sh $ck ${tag}_sf_quiet sf
done
echo "$(date -u) SF DONE" >> /workspace/runs/arms_done.txt
