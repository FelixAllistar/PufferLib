#!/usr/bin/env bash
# Lichess bot engine: RL best net (human BC + 12B RL, 1024x2) + small search. Swap PUFFER_MODEL/PUFFER_FEATS for
# the Stockfish-BC feature nets.
export PUFFER_MODEL=${PUFFER_MODEL:-$HOME/tools/puffer_chess_engine/rl_best.bin} PUFFER_FEATS=${PUFFER_FEATS:-0}
export PUFFER_SEARCH=${PUFFER_SEARCH:-2} PUFFER_K=${PUFFER_K:-5} PUFFER_MARGIN=${PUFFER_MARGIN:-50} OMP_NUM_THREADS=1
exec $HOME/puffertank/.venv/bin/python $HOME/tools/puffer_chess_engine/engine.py
