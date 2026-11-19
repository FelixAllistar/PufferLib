#!/usr/bin/env bash
# 256x2 validation arms (2026-11-17): from scratch, 200M steps each, one at a time; then top-move Maia (200 games per
# level) on policy and magnet. feat = -DCHESS_MOVE_FEATS build (fixed layout), plain = no per-move features.
source /workspace/env.sh; cd /workspace/chess_teach
arm() {  # name binary teacher_pct
  H=256 R=$1 BIN=$2 TP=$3 STEPS=200000000 CKI=200 ./bench.sh
  F=$(ls /workspace/runs/$1/checkpoints/chess/$1/0*.bin | sort | tail -n 1)
  (cd /workspace/chess_eval && ln -sfn /workspace/PufferLib/$2 puffer_chess && for ck in $F $F.magnet; do
     tag=$1; [[ $ck == *.magnet ]] && tag=${1}_magnet
     EVAL_EXTRA="--policy.hidden_size=256" MAIA_GAMES=200 timeout 1800 bash run_eval.sh $ck arm_$tag maia; done)
  echo "$(date -u) $1 trained and scored ($F)" >> /workspace/runs/arms_done.txt
}
arm a256_feat_teach puffer_chess_teach 0.3
arm a256_plain_base puffer_chess_plain 0
arm a256_feat_base puffer_chess_teach 0
arm a256_plain_teach puffer_chess_plain 0.3
echo "$(date -u) ALL ARMS DONE" >> /workspace/runs/arms_done.txt
