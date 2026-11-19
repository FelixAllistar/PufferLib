#!/usr/bin/env bash
# A/B (box 1 share: seed 75) for -DCHESS_MOVE_FEATS2 (2026-11-17): 256x2 from scratch, teacher 0.3, 200M steps, 2 seeds x {feat, feat2}
# (seed 73 feat = the existing a256_feat_teach). Each checkpoint: Maia 1100, 1000 games, policy + magnet, games
# logged (CHESS_GAMES_OUT) for failure_scan.py. Waits for main1 to stop. One training run at a time.
source /workspace/env.sh; cd /workspace/chess_teach
echo "$(date -u) arms3 start" >> /workspace/runs/arms_done.txt
ev() {  # run binary
  F=$(ls /workspace/runs/$1/checkpoints/chess/$1/0*.bin | sort | tail -n 1)
  cd /workspace/chess_eval; mkdir -p games
  for ck in $F $F.magnet; do tag=$1; [[ $ck == *.magnet ]] && tag=${1}_magnet
    r=$(CHESS_GAMES_OUT=$PWD/games/$tag.txt MAIA_LC0_PATH=$PWD/lc0bin MAIA_WEIGHTS_PATH=lc0/maia-1100.pb.gz MAIA_NODES=1 \
        OMP_NUM_THREADS=8 timeout 1800 /workspace/PufferLib/$2 eval $ck --headless --base.env_name=chess --env.mode=4 \
        --env.fen_curric_pct=1 --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=32 --vec.num_buffers=1 \
        --vec.num_threads=8 --base.eval_episodes=1000 --policy.hidden_size=256 2>&1 | grep -a CUDA_EVAL \
        | grep -o -E "perf=[0-9.]+|draw=[0-9.]+" | tr "\n" " ")
    echo "ab2_$tag maia1100-top $r (1000 games)" | tee -a results.txt ab2.txt
  done; cd /workspace/chess_teach
}
arm() {  # name binary seed eval_binary
  SEED=$3 H=256 R=$1 BIN=$2 TP=0.3 STEPS=200000000 CKI=200 ./bench.sh
  ev $1 $4; echo "$(date -u) $1 done" >> /workspace/runs/arms_done.txt
}
arm a256_feat2_teach_s75 puffer_chess_teach2 75 puffer_chess_teach2
arm a256_feat_teach_s75 puffer_chess_teach 75 puffer_chess_pgn
echo "$(date -u) ARMS3 DONE" >> /workspace/runs/arms_done.txt
