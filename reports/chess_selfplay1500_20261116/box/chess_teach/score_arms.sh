#!/usr/bin/env bash
# After the arms: Stockfish 1320 (1 s/move, 200 games) for the two feature arms, then a Maia-1100 curve over the
# feat_teach checkpoints.
source /workspace/env.sh; cd /workspace/chess_eval; ln -sfn /workspace/PufferLib/puffer_chess_teach puffer_chess
export EVAL_EXTRA="--policy.hidden_size=256"
for a in a256_feat_teach a256_feat_base; do
  F=$(ls /workspace/runs/$a/checkpoints/chess/$a/0*.bin | sort | tail -n 1)
  SF_GAMES=200 SF_LEVELS=1320 SF_AGENTS=11 SF_THREADS=11 timeout 3000 bash run_eval.sh $F arm_$a sf
done
for F in $(ls /workspace/runs/a256_feat_teach/checkpoints/chess/a256_feat_teach/0*.bin | sort | head -n -1 | tail -n +2); do
  s=$(basename $F .bin); MAIA_GAMES=200 timeout 900 bash run_eval.sh $F curve_feat_teach_$((10#$s)) maia
done
echo "$(date -u) SCORING DONE" >> /workspace/runs/arms_done.txt
