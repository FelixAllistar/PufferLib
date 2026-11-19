#!/usr/bin/env bash
# main3 (2026-11-18): size test, 2048x2 (main2 1024x2 ended .877/.874 vs Maia 1100 at 3B, magnet plateau ~.875 from 2.4B).
# Same as main2 (feat2, teacher 0.3, lr 0.00208645 -> 0.4x, entropy -> 0.1x, 3B, seed 73) except magnet_tau 0.02
# (256 test: .728/.722 vs .708/.682 at tau 0.005). Meant to move to a bigger box with --base.resume (keep state/).
source /workspace/env.sh; cd /workspace/chess_teach
H=2048 R=main3 BIN=puffer_chess_teach2 TP=0.3 STEPS=3000000000 LR=0.00208645 CKI=1000 SEED=73 ./bench.sh --train.magnet_tau=0.02
echo "$(date -u) main3 done" >> /workspace/runs/arms_done.txt
