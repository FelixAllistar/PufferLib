#!/usr/bin/env bash
# main2 (2026-11-17): main1 + the FEATS2 bits (allows mate in 1, check-led loss; +35-40 Elo at 256/200M over 3 seeds).
# Pure self-play from scratch, 1024x2, teacher 0.3 (default groups, no blunder bank), 3B steps (main1 was flat after
# ~3B of its 6B schedule), lr 0.00208645 -> 0.4x and entropy -> 0.1x over the 3B, magnet 0.003 / tau 0.005, a
# checkpoint every ~262M. No pauses: checkpoints are scored beside training (Maia 1100, 1000 games, one eval at a time).
source /workspace/env.sh; cd /workspace/chess_teach
H=1024 R=main2 BIN=puffer_chess_teach2 TP=0.3 STEPS=3000000000 LR=0.00208645 CKI=1000 SEED=73 ./bench.sh
echo "$(date -u) main2 done" >> /workspace/runs/arms_done.txt
