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
