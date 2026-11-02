# Chess (2026-11-01, poker box)

Yardstick: Maia-1100 (lc0 built from LeelaChessZero release/0.31 with the BLAS/Eigen CPU backend, CSSLab maia-chess
v1.0 weights, 1 node) and a random mover. chess_watch.sh scores every checkpoint against both.

## v0: the env as it was (pick a piece, then a square; 97 actions), MLP 512x3, self-play, 5B steps
Maia-1100: 1.4-2.8% at every checkpoint including the untrained one, all draws, 0 wins. Random mover: 39% untrained
-> 66% at 3.4B (61% draws: it cannot checkmate). maia_evals.txt.
Encoder problems found: the board was 64 piece codes (pawn = 1 ... enemy king = 12) fed as plain numbers, and the
trainer casts uint8 observations to float unscaled, so flags arrived as 255.

## v1: one action per move, 0/1 observation (chess.h)
4096 from-to actions in the mover's view (+ pass), queen promotions only; observation = 12 one-hot piece planes,
legal-from and legal-to squares, side, castling, en passant, rule-50 thermometer, repetition, checks; all 0/1.
2048 games in parallel (4097-way head with the poker sweep on the same GPU), 1B steps, ~180K SPS. evals in
v1_onestep/evals.txt on the box.
- v1/v1b/v1c and the SE-ResNet (chess_encoder.cu, CHESS_SENET; 17K SPS vs 180K for the MLP) never learned: entropy
  froze near uniform over legal moves. Not a bug: with the 4097-way head the Muon update was too weak at the
  inherited learning rate 0.00057. From mate-in-1 positions vs a random mover: v1 flat at lr 0.00057, 0.50 -> 0.73
  (ceiling ~0.75) at lr 0.003; the old 97-action env learned it at the old rate (0.48 -> 0.70).
- Even from K+Q vs K, near-random play never mates within the 50-move rule, so plain self-play has almost no win
  signal. Position banks (ocean/chess/tools/gen_fens.py): 20K mate-in-1, 20K KQK/KRK, 50K positions from Maia-1500
  self-play (2-10 random opening plies), shuffled into resources/chess/fens.txt.

## v2: one move per action, lr 0.003, self-play with fen_curric_pct 0.5 (v2_bank, stopped at ~1B)
Random mover from the start position: 0.49 -> 0.80 (66M) -> 0.97 (131M), then 0.94-0.97. Maia-1100: 1% -> 3-7%
from ~300M to ~1B steps (mostly draws, ~0-1% wins; ~210 games per eval), no trend. Stopped to free the GPU.
Next levers: Maia opponents in training (one lc0 per env, CPU-bound) or the SE-ResNet at lr 0.003.
