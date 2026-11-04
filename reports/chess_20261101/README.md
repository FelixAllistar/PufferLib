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

## v3 (2026-11-02): too many changes at once
1024x2, lr 0.01, minibatch 4096, gamma 0.9995, clip 0.2, vf 0.5, magnet 0.03, paired critic, opening bank: 26K SPS
(the 4097-way sampler/loss kernels loop all actions per row; width x2, minibatch /2) and 52% vs random at 65M. v3b
(same, magnet off): 63% at 65M. Stopped; went back to v2's config and added one change at a time.

## v2pz: v2 + zero-sum paired critic (puffer_chess_pz), bank = openings + v2 mix (150K), fen_curric_pct 0.5
~190K SPS alone. Random: 99.8% at 65M (v2: 80%), then 77-97%. Maia-1100: 3-5% to 500M, then 6-10%, best 12.8% at
1.31B (mostly draws, ~0.5-1.5% wins). Slowly rising; ~210 games per eval.
## v2pz_mag: the same + magnet 0.03 / tau 0.01 (A/B, sharing the GPU)
Random: 80% at 65M (slower start), then steadier (94-98.5%). Maia-1100 at equal steps about even with v2pz
(e.g. 655M 5.7 vs 6.5%, 721M 7.9 vs 6.9%, 786M 8.6 vs 8.0%).

## xap_italian (2026-11-03): attention-head Transformer, magnet, Italian bank: stopped at 65M
Entropy stuck at 1.40 from ~2M steps, KL ~0, clipfrac 0. Random 51% and Maia-1100 1.6% at 65M (step 0: 51% / 1.4%);
scattered first moves. Then all generic Transformer code was removed (commit 110891ce8).

## rules / rules_book (2026-11-03): v2pz_mag recipe + rule features (+ Italian book reward)
Build `NVCC_EXTRA="-DCHESS_RULES -DCHESS_PAIRED" bash build.sh chess ./puffer_chess_rules`. CHESS_RULES adds six
64-square planes (obs 923 -> 1307): squares attacked by each side, pieces en prise for each side (attacked and
undefended, or attacked by a cheaper piece) and pinned pieces; checked against python-chess on 3000 random positions
(0 mismatches). `env.book_reward` pays the mover per Italian book move (Giuoco Piano to 4...d4, Two Knights / Fried
Liver to 5.exd5; 13 book moves) and takes it from the other side (zero-sum, for the paired critic); `book_moves` (per
game) is logged either way. Arms on one GPU (~69K SPS each, 6.8 GB each): rules (book 0), rules_book (book 0.05).
Watcher: chess_watch5.sh (one eval at a time, RAND_AGENTS=64: 256 agents ran out of memory beside two trainings).

### rules_book result (1B steps) and big evals (2026-11-03, rules_book/big_eval/results.txt)
Italian through move 4 every game (1.e4 265/265, ...e5 9/9, 2.Nf3 10/10, ...Nc6). Big evals, every game replayed with
python-chess (big_eval.sh / classify.py; every win is a checkmate, draws are mostly threefold repetitions):
| opponent | checkpoint | games | score | wins | draws | losses |
|---|---|---|---|---|---|---|
| Maia-1100 (lc0, 1 node) | 721M | 1005 | 21.6% | 12% | 20% | 68% |
| Maia-1100 | 852M | 1000 | 23.2% | 12% | 22% | 66% |
| Maia-1100 | 917M | 1005 | 23.4% | 13% | 20% | 66% |
| Maia-1100 | 983M | 1003 | 22.0% | 13% | 17% | 69% |
| Maia-1100 | 1B | 1008 | 18.2% | 10% | 16% | 74% |
| Maia-1500 | 917M | 417 | 14.4% | 1% | 26% | 73% |
| Stockfish 16, UCI_Elo 1320 (its floor), 100 ms | 917M | 430 | 1.0% | 1% | 0% | 99% |
The 200-game swings (9-34%) were noise: from ~700M it is flat at ~210 Elo under Maia-1100 (Lichess ~890 by the Elo
formula), and the final 1B checkpoint is a little worse (LR annealing to the end is a suspect). Maia-1500's score is
almost all repetition draws. Stockfish's limit-strength floor plays far above a human 1320 at this speed, so it is not
a useful anchor here; nothing calibrated exists below Maia-1100 / Stockfish 1320.

## sweep_book (2026-11-03): book-move sweep
config/chess.ini: rules_book recipe with entropy 0 and no LR annealing, 50M steps per trial, 500 trials, score =
env/book_moves at the end of training, early stopping off. Locked: network 512x3, replay ratio 1, entropy 0, book
reward 0.05, FEN curriculum 0.5. 18 dims: agents 1024-4096, horizon 32-128, minibatch 2048-16384, lr, gamma, lambda,
clip, vf clip, vf coef, grad norm, momentum, frozen-opponent share, magnet coef/tau, V-trace on/off + rho/c clips,
opponent refresh. Memory caps agents x horizon at 4096 x 64 (11 GB; 8192 x 64 and 4096 x 128 fail to allocate); those
corners fail in seconds and count as failed trials (max_consecutive_failures 100). History seeded with rules_book as
trial 0 (book_moves ~0.37 at 50M; that run still had entropy 0.01 annealed and LR annealing). tmux chess_sweep.
The first 12-dim start (sweep.history.first_12dim) was stopped before any trial finished.
