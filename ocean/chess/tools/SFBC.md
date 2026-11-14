# Stockfish BC, per-move lookahead features, RL from BC (2026-11-12 .. 11-14)

Pipeline: Lichess games -> Stockfish labels -> supervised BC net -> Maia eval -> (optional) DAgger -> native RL.

| Step | Files |
|---|---|
| Games | `lichess_games.py` (monthly dump, rating filter), `gen_states.py` |
| Stockfish labels (depth 8) | `sf_label.py`, `box/label2.sh`, `box/q8.sh` |
| Supervised BC | `bc_train.py` (+ `bc_encode.c` -> `build/chess/libbc_encode.so`, build line in its header); `--move-feats` adds the per-move features |
| Per-move lookahead features | `../chess_tactics.h`: for each legal move, play it, let the opponent resolve captures (material quiescence), bucket the swing (5 buckets), + gives-check, + mates |
| Eval vs top-move Maia | `play.py` (`--maia 1500`, `--search 2` = play-time 2-ply material check of the policy's top 5, `--no-repeat` = 2-fold guard when ahead), `box/evalm.sh` |
| DAgger | `selfplay_gpu.py` / `selfplay_games.py`, `box/dagger.sh` |
| Native RL with the features | build with `NVCC_EXTRA="-DCHESS_RULES -DCHESS_PAIRED -DCHESS_MOVE_FEATS"`; `mf_to_native.py` reorders a BC checkpoint (and `--reverse` back for `play.py`); `box/run_mf.sh`, `box/evalwatch.sh` |
| Lichess bot engine | `lichess/engine.py`, `lichess/run.sh` (token never in the repo) |
| Diagnostics | `diag/phase_grade.py`, `diag/phase_report.py`, `diag/state_test.py` |

`play.py` needs `libchesslib3.so` next to it:
`cc -O2 -shared -fPIC -std=gnu2x -DCHESS_RULES -Isrc -Iraylib-5.5_linux_amd64/include -Iocean/chess ocean/chess/tools/chesslib2.c ocean/chess/tools/raylib_stubs.c -o ocean/chess/tools/libchesslib3.so`

`box/*.sh` are the scripts as run on a Vast box (paths under /workspace).

Results (top-move Maia 1500, 1 node, 300 games; greedy / with search):
- RL best (human BC + 12B RL, 1024): 1542 / 1813
- Stockfish BC 1M games, 2048 + features: 1647 / 1788; with the 2-fold guard 1713 / 1854
- Native RL from it, first attempt: 1425 / 1672 at 524M steps. Cause: `mf_w` was {1, 7}, so Muon normalized each
  update to a unit vector and the feature weights blew up (-0.28 -> -4.9). Fixed: `mf_w` is 1-D (Muon skips it).
  The fixed run has no result yet.
