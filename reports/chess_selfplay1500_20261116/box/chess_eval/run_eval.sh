#!/usr/bin/env bash
# Calibrated eval of a chess checkpoint (2026-11-06): games start from 1000 Maia-1900-sampled 8-ply openings (random
# line per game, colors alternate), against top-move Maia 1100/1500/1900 (1 node, the way Maia is meant to play) and
# Elo-limited Stockfish 16 (1 s/move). Runs in /workspace/chess_eval so its resources/chess/fens.txt (the openings)
# doesn't touch the training tree. Score = (wins + draws/2) / games. Usage: run_eval.sh CKPT TAG [maia|sf|all]
cd /workspace/chess_eval
CK=$1; TAG=$2; WHAT=${3:-all}; OUT=results.txt
ev() {  # opponent-env... games agents threads
  env "${@:1:$#-3}" OMP_NUM_THREADS=${@: -1} timeout 14400 ./puffer_chess eval $CK --headless --base.env_name=chess \
    --env.mode=4 --env.fen_curric_pct=1 --selfplay.enabled=0 --vec.num_policies=1 --vec.total_agents=${@: -2:1} \
    --vec.num_buffers=1 --vec.num_threads=${@: -1} --base.eval_episodes=${@: -3:1} ${EVAL_EXTRA} 2>&1 \
    | grep -a CUDA_EVAL | grep -o -E "perf=[0-9.]+|draw=[0-9.]+" | tr "\n" " "
}
if [[ $WHAT != sf ]]; then
  for w in 1100 1500 1900; do
    r=$(ev MAIA_LC0_PATH=$PWD/lc0bin MAIA_WEIGHTS_PATH=lc0/maia-$w.pb.gz MAIA_NODES=1 ${MAIA_GAMES:-600} 32 8)
    echo "$TAG maia$w-top $r" | tee -a $OUT
  done
fi
if [[ $WHAT != maia ]] && [ ! -f /workspace/chess_eval/NO_SF ]; then  # touch NO_SF to skip Stockfish (main1 pauses: Maia only)
  for e in ${SF_LEVELS:-1320 1600 1900 2200}; do
    r=$(ev MAIA_LC0_PATH=$PWD/uci_stockfish_elo.py SF_ELO=$e SF_MS=${SF_MS:-1000} ${SF_GAMES:-300} ${SF_AGENTS:-24} ${SF_THREADS:-24})
    echo "$TAG sf$e $r" | tee -a $OUT
  done
fi
