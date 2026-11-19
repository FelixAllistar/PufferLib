#!/usr/bin/env bash
# Lichess bot engine (2026-11-19): main2 = pure self-play RL (no human games, no behaviour cloning), 1024x2 MinGRU,
# 3B steps, + the 2-ply material check of the policy's top 5 moves.
export PUFFER_MODEL=${PUFFER_MODEL:-$HOME/tools/puffer_chess_engine/main2/main2.bin}
export PUFFER_SEARCH=${PUFFER_SEARCH:-2} PUFFER_K=${PUFFER_K:-5} PUFFER_MARGIN=${PUFFER_MARGIN:-50} OMP_NUM_THREADS=1
exec $HOME/puffertank/.venv/bin/python $HOME/tools/puffer_chess_engine/main2/engine2.py
