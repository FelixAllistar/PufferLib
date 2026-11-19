# Chess: pure self-play to 1500 (started 2026-11-16)

Running notes. Newest entries go in the Log at the bottom; the sections above it are kept current.

## Goal and rules (Felix, 2026-11-16)
- Beat an honest 1500 Elo with PufferNet (1024x2 MinGRU), **self-play only**: no behavior cloning, no search at play
  time. "How far can we get with puffernet."
- Levers: state sets, EMAgnet (magnet), iterating on failure states with a probe, observation changes, and a bandit
  teacher over state sets (as in mario_sim / Pokemon).
- It has to be cheap: Vast credit is $3.52 on 2026-11-16, so everything that can be settled on the local CPU is
  settled there first. The local GPU (3 GB) is in use by another session.
- Honest Elo only (memory: maia-eval-sampled): lean on the lowest anchor, call out outliers, never headline the
  flattering one. Stockfish's limited-Elo strength depends on move time and CPU: compare only same-setup runs.
- Good notes for every run from now on: settings, the exact start checkpoint, the states file, results.

## Where we are (all 1024x2, greedy policy, no search)
Calibrated eval = /workspace/chess_eval/run_eval.sh on the box: top-move Maia from held-out openings (300 games per
level) and Stockfish 16 UCI_Elo 1320 at 1 s/move (200 games). Elo below = 400 log10(s / (1 - s)) + opponent label.

| Model | What | Maia 1100 | Maia 1500 | Maia 1900 | SF 1320 | Honest Elo |
|---|---|---|---|---|---|---|
| control2 6B magnet | self-play, W/L/D only, from wld 2B | .497 | .324 | .219 | .184 | ~1100 |
| openings_0.9 6B magnet | + 90% of games from 38K Maia openings | .593 | .369 | .302 | .270 | ~1150 |
| **states_cont 6B magnet** | + 6B more on the 81K "states_100k" set | .645 | .487 | .391 | .340 | **~1200** |
| states_cont 6B policy | same run, policy | .592 | .440 | .362 | .298 | ~1170 |
| rl_best (bcrl_cont) | human BC + 12B RL, magnet frozen at BC | .706 | .520 | .425 | .363 | ~1230 |
| rl_best + 2-ply search | Lichess bot setting | | | | .650 | ~1430 |

- Best pure self-play = `states_cont` final magnet, ~1200. Its lineage is 14B steps: wld 2B (from scratch) ->
  openings_0.9 6B -> states_cont 6B. Files: `/mnt/d/puffer_media/box_backups/chess_box_60081973/chess_20261101/states_cont/`.
- Maia 1500 / 1900 give 1490 / 1820 for the same net: Maia's labels are squashed together, so those are outliers.
- Correction 2026-11-16: "rl_best with search ~1580" came from 60 local games at 0.5 s/move (.817). The 200-game box
  run at 1 s/move says .650 = ~1430. The GitHub release notes and memory now say ~1430.

### states_cont recipe (the settings to start from)
2048 agents, horizon 128, minibatch 8192, lr 0.000836 annealed to 0.4x (the lineage started at 0.00209), gamma 0.9947,
lambda 0.871, clip 0.49, vf coef 4.81, vf clip 1.57, grad norm 1.17, momentum 0.916, entropy coef 0.00058 (fixed),
magnet coef 0.003 / tau 0.005 (forward KL), hist policy share 0.0625, fen_curric_pct 0.9, replay ratio 1, no V-trace.
Build `-DCHESS_RULES -DCHESS_PAIRED`. Full command: `states_cont.sh` in the backup folder.
- Policy entropy at the end: 0.06 nats (almost deterministic). Draw rate 0.156, 93 plies per game.
- The BC runs (bcrl, bcrl_cont, mfrl) all had magnet_tau = 0: the magnet stayed the BC net for the whole run. That
  was the written plan on 2026-11-10; Felix (2026-11-16): it should not have been 0. Never tau 0 again.

### Speed (measured)
- states_cont on an RTX 5060 Ti, 14 threads: ~116K SPS. Per epoch: rollout forward 2.0 s (51%), train 1.7 s (43%),
  copy 0.2 s, **env 25 ms (0%)**. The same recipe on an RTX 5090 ran 411K SPS (env 17 ms).
- So the network is all of the cost. The plain env and any cheap observation bits are free.
- Per-move lookahead features (chess_tactics.h): 791 us per position on the local CPU vs 6.3 us for position setup
  plus legal moves (20K positions from states_100k, 33.7 legal moves each, scratch bench_mf.c). That is 125x the base
  env cost and why the 2048 + features run did 70-88K SPS with env 2.7 s per epoch. Cause: every quiescence node
  calls the full `generate_legal` and rescans 64 squares for material (~5 us per node, ~4.7 nodes per root move).
- The 4097-way head is about a third of the net's multiply-adds (decoder 4.2M of ~12.2M per row).

## What the per-move lookahead features are worth (existing data, Stockfish-BC, same 200K games, same 1024x2)
| Net | SF 1320 greedy (200 games, 1 s/move) | Maia 1500 greedy (300) |
|---|---|---|
| sfbc200k_1024 (no features) | .200 (~1080) | .223 |
| sfbc200k_1024_mf (features) | .500 (~1320) | .562 |
About +240 Elo from the features alone. No self-play run has ever had them (mfrl started from Stockfish BC).

## Findings 2026-11-16 (local CPU, scripts in this folder)

### 1. Endgame probe (`endgame_probe.py`, results in `probe/`)
Mate in 1, top policy move mates (150 positions per cell; the critic's value was +0.87..+0.95 in all of them):
| Model | KQK corner | KQK edge | KRK corner | KRK edge |
|---|---|---|---|---|
| states_cont magnet (best self-play) | .37 | .47 | .27 | .67 |
| states_cont policy | .40 | .47 | .27 | .67 |
| rl_best (BC + RL) | .71 | .67 | .55 | .89 |

Conversion: the model plays the strong side from 60 random positions against full-strength Stockfish (4,000 nodes),
env draw rules. Wins out of 60:
| Set | Stockfish as strong side | states_cont magnet | states_cont policy | rl_best |
|---|---|---|---|---|
| KQK / KRK / KRRK | | 60 / 60 / 59 | 60 / 58 / 59 | 60 / 60 / 60 |
| KQPP v KPP | 59 | 54 | 56 | 58 |
| KQR v KR | 59 | 55 | 55 | 53 |
| KRPP v KPP | 56 | 46 (10 lost) | 46 (8 lost) | 48 (8 lost) |
| KQNPP v KRPP | 58 | 44 (9 lost) | 39 (14 lost) | 37 (13 lost) |
- Bare-king mates are fine (it mates, ~15-22 plies, it just rarely takes the mate in 1).
- The failures are won positions where the other side still has pieces: it gets checkmated or drops material
  (8-14 of 60), plus a few stalemates and threefold repetitions. BC + RL is no better here.

### 2. Is it blind? (`obs_probe.py`)
A fresh 256x2 net trained directly on 18K mate-in-1 positions (the old `banks/mate1.txt`) reaches 99% on 2K held-out
positions in 250 steps of batch 512 (2 minutes on one core), corners included: Q corner 1.00, R corner 1.00, Q edge
.99, R edge 1.00. Adding king features (flight squares, counts, king distance) changes nothing (1.00).
=> The observation and the net can see these mates. The misses are a training-state gap: no run since v2 has had
endgame or won-position starts (states_100k is openings and early middlegames, <= 30 plies, within +-150 cp).

### 3. Thrown wins in real games (rl_best vs SF 1320, 200 games on the box)
In 83 games it was up >= 5 points of material for 4+ plies. It failed to win 20 of them: 16 losses by checkmate,
2 stalemates, 1 threefold, 1 insufficient material. Winning those would be +0.09 score (~+65 Elo). Against Maia 1500:
97 such games, 6 not won.

## Plan (draft, 2026-11-16; open decisions marked)
1. **Fast per-move features.** Same outputs, bit-for-bit (checksum test on 100K positions), 10x+ faster: captures-only
   generation in the quiescence nodes, material kept incrementally, a narrow search window around the bucket edges.
   Target <= 60 us per position. Then add two cheap bits: "repeats an earlier position" and "allows mate in 1".
2. **State groups + teacher (env-local).** Fixed shares: standard start / opening-middlegame states / teacher pool.
   Teacher pool = endgame banks plus an online buffer of the policy's own thrown wins (snapshot taken when a side
   first goes >= +5 material for 4 plies; kept when that side does not win). Per group: natural conversion rate,
   practice conversion rate, weight by 4p(1-p), learning progress |fast - slow|, staleness, uniform floor (the shared
   math in ocean/retro/curriculum_teacher.h).
3. **Legal-move head.** Compute logits only for the <= 128 legal moves instead of all 4,097 (same weights, same
   checkpoints). Estimated 1.3-1.5x SPS; needs a GPU to measure.
4. **One paid run**, scored with the calibrated eval plus `endgame_probe.py`.
   OPEN (Felix): continue the ~1200 self-play net with the new inputs zero-initialised (cheap, keeps 14B steps), or a
   fresh run from scratch (clean story, but most of the budget goes to relearning 1200).
5. Sweeps: none on the current budget. When there is money: short branches from one shared mid-run checkpoint,
   scored on the real thing (fixed opponents from held-out openings + the probes), never on a proxy like book moves.

## Log
- 2026-11-16: goal set; baselines, recipe and speed collected from the backups; endgame probe, observation probe,
  thrown-wins count and feature benchmark run locally (above). Release notes corrected to ~1430 with search.
- 2026-11-16: local scoring started (`score_local.sh`: states_cont magnet, mfrl 1.08B, sfbc1m_mf_2048; 100 games each
  vs SF 1320 at 500 ms/move on the local machine). mfrl's magnet file equals sfbc1m_mf_2048.bin (tau 0), so
  "mfrl vs its start" is also policy vs magnet. Results: pending.
- 2026-11-16: bug fixed in `play.py` and the Lichess `engine.py` (live copies in ~/tools/puffer_chess_engine; the
  pushed copies in ocean/chess/tools still have it). Top-k was taken over probabilities; for a very confident policy
  the other moves' probabilities underflow to exactly 0 and tie with illegal moves, so an illegal move could be
  picked -> IndexError in action_uci. This is the crash that killed 7 of 40 eval jobs on 11-14 and 2 of 3 scoring
  jobs today. Now top-k runs over the masked logits. The mask itself is right (37,784 random positions: every masked
  action has a legal move). The bot's log shows no engine crash from it (its 16 tracebacks are network stream
  errors). Scoring restarted.
- 2026-11-16 local scoring result (`score_local.sh`, 100 games each, greedy, SF 1320 at 500 ms/move, local machine;
  only comparable with each other and with rl_best's earlier local .425 over 60 games):
  | Model | Score | W/D/L | Elo vs 1320 |
  |---|---|---|---|
  | sfbc1m_mf_2048 (Stockfish BC + per-move features, no RL; = mfrl's start and frozen magnet) | .625 | 56/13/31 | ~1410 |
  | states_cont 6B magnet (best pure self-play) | .285 | 27/3/70 | ~1160 (box, 1 s/move: .340, ~1205) |
  | mfrl 1.08B policy (RL from the first row, magnet tau 0, coef 0.003) | .060 | 6/0/94 | ~840 |
  mfrl is a collapse, not a load error: its critic tracks results (last value x result 0.72) and the magnet file
  converts back to sfbc1m_mf_2048.bin byte for byte. RL from the Stockfish-BC net unlearned it (the earlier Muon-bug
  run had also dropped, Maia 1500 greedy 1713 -> 1425 at 524M). Do not resume mfrl. First honest number for the
  Stockfish-BC 2048 net: ~1410, policy only.
- 2026-11-16: **per-move features made ~4x faster, same outputs** (`ocean/chess/chess_tactics.h`, uncommitted).
  Changes: material from `pieceCount` instead of a 64-square scan; quiescence nodes not in check generate only legal
  captures / en passant / queen promotions (`ct_gen_tactical`) and use a cheap "any legal move" test for stalemate;
  the stand-pat cut happens before move generation; a capture the opponent can answer by standing pat below alpha is
  skipped (exact: taken only when the opponent is not in check and has a move); root window narrowed to +-251 around
  the current material (only the bucket is used); a 4,096-entry per-thread table of results keyed by position + ply.
  Test `ocean/chess/tools/test_tactics.c` against the frozen first version (`ocean/chess/tools/ct_ref.h`), 0 mismatches:
  | Positions | Old | New | Speed-up |
  |---|---|---|---|
  | all 81,254 states_100k | 322 us | 82 us | 3.9x |
  | 40K KQK/KRK + mate-in-1 bank | 6.6 us | 4.7 us | 1.4x |
  | random 60-ply playouts from 3,000 states + 2,000 endgames (checks, promotions, 3,218 mates) | 706 us | 135 us | 5.2x |
  (local CPU, one idle core; the 791 us measured earlier today was under load.) Nodes per position 594 -> 388 at
  ~200 ns each; median position 221 nodes, top 10% of positions = 42% of nodes.
  Tried and dropped: one-point windows at the bucket edges (exact, but 88 us vs 82). Compiler flags change little
  (-O3 -march=native: 73 us). Left on the table: a captures-only path for in-check nodes (they are 12% of nodes and
  still use the full generator, ~15% of time).
  Estimate, not measured: the 2048 run's env was 2.7 s per epoch on 56 threads; at 4x that is ~0.7 s, against a GPU
  epoch of ~0.64 s for 1024x2 on a 5090. Needs a box to confirm.
- 2026-11-16: Felix: go ahead; self-play only; continue the ~1200 net. Maia 1100 for that net = .645 (~1200).
- 2026-11-16: **state-group teacher built** (uncommitted): `ocean/chess/chess_teacher.h`, hooks in `chess.h`
  (reset, move, game end, log, init/close), `env.teacher_pct` in config/chess.ini (default 0 = off).
  - It reuses the shared teacher math (`ocean/retro/curriculum_teacher.h`), so it needs C++; the trainer compiles
    chess.h as C++ (nvcc), the C tool builds get only the counters.
  - Teacher pool = bank groups (`resources/chess/teacher_groups.txt`) + 9 "lead classes" filled online with the
    policy's own thrown wins: in a natural self-play game the first position where a side has led by >= 5 points
    for 4 plies is remembered; if that side does not win, the position is stored (ring of 4,096 per class) and the
    class gets failure mass. Classes = lead size (5-7, 7-10, 10+) x what the other side has left (none / <= rook / more).
  - Practice outcome = the side ahead by >= 3 points at the start wins. Weights: failure mass x 4p(1-p) 0.5,
    learning progress 0.35, staleness 0.15, floor 0.1 (header defaults).
  - New dashboard values: `lead_n`, `lead_win` (natural games with a stable lead and how many the leader won; these
    are logged even with the teacher off) and `teach_n`, `teach_win`.
  - State saved to `$CHESS_TEACHER_STATE` every 50M agent steps and on close (+ a readable `.txt` table), reloaded
    at start, so pause / resume keeps the rings and statistics.
  - Tests (`ocean/chess/tools/test_teacher.cpp`, CPU, 4 OpenMP threads, capture-happy random policy): all checks
    pass (teacher share .296 vs .30, the teacher's books equal the env log, every stored state is a lead of its
    class, save / reload identical). With teacher_pct 0 the env is unchanged: the same hash over 8,181 games as the
    chess.h from before the change. Also builds and passes with -DCHESS_PAIRED -DCHESS_MOVE_FEATS.
  - Bank groups generated without any engine (`ocean/chess/tools/gen_teacher_banks.py`, 20K random legal positions
    each, seed 777): kqk krk krrk kbbk kqkr kqrkr kqppkpp krppkpp knppkpp kqnppkrpp krrppkrpp krnppknpp kqbpppkrnppp,
    in /mnt/d/puffer_media/chess_bc/teacher_banks (+ teacher_groups.txt).
- 2026-11-16: `ocean/chess/tools/add_move_feats.py` adds zero per-move feature weights to a plain checkpoint.
  Checked on states_cont's magnet: logits and value differ by exactly 0 on 200 positions; file size 48,147,484.
- 2026-11-16: run drafted, not started: `run_teach1.sh` (features + teacher 0.3 on the states_cont net, lr 0.0005).
  Things to watch in that run:
  - `mf_w` (7 feature biases) is 1-D, and the optimizer gives 1-D parameters a plain small step (it did not move
    in 1B steps of mfrl). Starting from zero, the features have to act through `mf_u` (7 x hidden). Check |mf_u|
    in early checkpoints (mf_to_native.py --reverse prints it); if it stays ~0 the features are not being used.
  - mfrl's collapse had two likely causes besides the frozen magnet: it restarted at the full lr 0.00209 on a
    converged net. teach1 restarts at 0.0005.
  - Possible old bug, not touched: `my_vec_close` frees the shared fens.txt list for any env set, so an in-run bot
    eval closing its envs would free the list the training envs still use. teach1 keeps in-run evals off.
- 2026-11-16: final local check with everything together (trainer flags -DCHESS_RULES -DCHESS_PAIRED
  -DCHESS_MOVE_FEATS, 512 envs, 6,000 steps, all 13 banks = 260K positions): all teacher checks pass, one lead ring
  filled to its 4,096 cap and wrapped, teacher-off hash still equals the old env. A lead is not remembered on a
  ply that ends the game or has rule-50 > 80 (such a position is useless as a start state). Feature test also run
  on 257,822 bank / playout positions: 0 mismatches. Ready for a box; nothing committed.
- 2026-11-16: **local smoke test of the real trainer** (GTX 1060 3 GB, binary `puffer_chess_teach` built with
  -DCHESS_RULES -DCHESS_PAIRED -DCHESS_MOVE_FEATS; script `build/chess/smoke/smoke.sh`: 256x2 from scratch, 512
  agents, horizon 64, teacher_pct 0.3, 4 threads). 1.5M steps, exit 0, ~27K SPS; env (with the per-move features)
  15-22% of the epoch, rollout forward ~58%, train ~41%.
  - Teacher inside the trainer: lead rings fill from the net's own games (lead7_heavy: 1,142 natural leads, leader
    won 40%, 682 stored), practice outcomes recorded for all 22 groups, state file written on exit.
  - Restart with the same CHESS_TEACHER_STATE: the teacher's clock went on from 1,474,560 to 2,654,208 steps, so
    the state reloads.
  - teach_n / teach_win / lead_n / lead_win moved up in puf_log so the dashboard shows them (it cuts the list
    after ~20 rows); they are also in the run's log file as env/teach_n etc. Fresh 256x2 net at ~1.4M steps:
    teacher games 0.27-0.34 of all, leader converts ~45-51% of stable leads.
- Felix (2026-11-16 evening): will rent a box and can add ~$10-14. Wants the ratios learned, not hand-set: next
  version = score every group by the critic's value error (Prioritized Level Replay style), whole game and openings
  inside the pool, clusters less hand-made, score-based pruning; iterate on 256x2. Puzzles (Lichess puzzle
  database themes, positions only) as spatial groups: his idea, agreed. Order agreed in outline: measure SPS on the
  box, short 256x2 validation arms (teacher on/off, features on/off), then the main continuation (run_teach1.sh).
- 2026-11-17 (UTC) box: Vast `ssh -i ~/.ssh/vast_ed25519 -p 20361 root@ssh5.vast.ai`, RTX 5060 Ti 16 GB, CPU quota 11.5
  cores (EPYC 7K62), CUDA 13.2. Setup: /workspace/PufferLib (11-12 bundle + today's src, config, build.sh, vendor,
  ocean/chess, ocean/retro/curriculum_teacher.h), /workspace/env.sh, /workspace/chess_teach (bench.sh launcher,
  fens.txt = states_100k, teacher_groups.txt), /workspace/teacher_banks, /workspace/ck (states_cont 6B + converted
  sp1200_mf.bin/.magnet), /workspace/chess_eval (lc0 + Maia + Stockfish 16). Needed: uv pip nvidia-nccl-cu13 (+ a
  libnccl.so symlink), apt ccache libgl1-mesa-dev libomp-dev stockfish. Binaries: puffer_chess_teach (features +
  teacher), puffer_chess_plain (no features), puffer_chess_oldtac (old feature code).
- Speed, features + teacher, 2048 agents, horizon 128, 11 threads, 2 buffers, from scratch:
  256x2 ~167K SPS (env 24%), 512x2 ~152K, 1024x2 ~112K (env 10%); 1024 loaded from the 1200 net ~107K.
  (states_cont without features on a 5060 Ti: 116K.) From-scratch runs learn: 256x2 at 16M steps converts 66% of
  teacher games.
- **BUG, OPEN: loaded nets play wrongly in the -DCHESS_MOVE_FEATS build.** Maia 1100, 100-200 games:
  plain build + plain states_cont magnet .636 (correct); features build + the same net with zero feature weights
  .0125; same with the OLD chess_tactics.h .014 (so not today's speed-up); Stockfish-BC features net
  sfbc1m_mf_1024 natively, greedy, vs Maia 1500 .236 where play.py gives .632. In training the loaded net shows it
  from epoch 1: draw rate .77, entropy .015, old_kl .08-.15. So the native features path does not compute what
  play.py / the plain build compute, and has not since 11-14: mfrl trained on it, which probably explains its
  collapse. Ruled out: file order (sizes match, zero weights), the lookahead leaving the position, history or
  legal list changed (300K positions, 0 changes), eval tooling (plain build is right). Not yet checked: the
  prefix encoder (chess_prefix_encoder.cu) against the default encoder inside the trainer, the decoder's
  MOVE_FEATS code on empty slots (code 255), and whether the step-0 checkpoint the trainer saves equals the file
  it loaded. Next test: a build with features on but the default encoder and an encoder zero-padded to 1,691
  columns; if that plays .64 the prefix encoder is the fault.
- Nothing is training on the box. Do not start teach1 until the bug is fixed.
- 2026-11-17 **BUG FOUND AND FIXED: parameter layout in every -DCHESS_MOVE_FEATS build since 11-14.**
  Cause: the trainer's allocator starts every tensor on a 16-byte boundary (`alloc_create` in src/pufferl.cu), but
  checkpoints, Muon and the magnet treat the parameters as one flat array of `total_elems` floats with no gaps.
  `mf_w` had 7 floats (28 bytes), so `mf_u` and both MinGRU matrices sat one float later than the flat view says.
  Effects: (1) a loaded checkpoint got its recurrent weights shifted by one element (the 1200 net scored .01 vs
  Maia 1100 in the features build, .636 in the plain build); (2) Muon orthogonalised shifted copies of mf_u and
  the MinGRU matrices in every features run, from scratch or not; (3) the last MinGRU float was never saved or
  trained. mfrl and the earlier "Muon bug" run both trained on this, so their collapse says nothing about RL on a
  Stockfish-BC net or about the features. The +240 Elo feature result is unaffected (PyTorch BC + play.py).
  How it was found: plain build right, features build wrong with old and new feature code, with the default
  encoder (zero-padded) and with the decoder bias compiled out; base observations and game results identical with
  and without the flag (test_teacher.cpp now hashes them); a fresh checkpoint has 1,800 zero floats where 1,799
  were expected (7 + 1 gap + 7 x 256).
  Fix (env-local, ocean/chess/chess_decoder.cu): `mf_w` and its gradient are stored with 8 elements
  (CHESS_MF_STORE; element 7 unused and always 0). `mf_to_native.py` and `add_move_feats.py` write / read the
  8-float mf_w. Native feature checkpoints from before the fix cannot be used.
  Verified on the box (Maia, top move): converted states_cont magnet .623 vs Maia 1100 over 200 games (plain build
  .636, original eval .645); sfbc1m_mf_1024 native greedy .775 vs Maia 1100 and .702 vs Maia 1500 (play.py .632
  over 300; before the fix .41 / .24).
  Rule for the trainer: every parameter tensor must be a multiple of 16 bytes (4 floats). Nothing checks it.
- The speed numbers above were measured before the fix; the fix does not change the amount of work.
- 2026-11-17 **256x2 validation arms** (box, fixed build, from scratch, 200M steps each, 2048 agents, horizon 128,
  lr 0.002 -> 0.4x, entropy 0.0058 -> 0.1x, magnet 0.003 / tau 0.005, fen_curric_pct 0.9; `chess_teach/arms.sh`,
  one seed). Top-move Maia, 200 games per level, sampled policy (the box eval's default):
  | Arm | Maia 1100 | Maia 1500 | Maia 1900 | natural leads converted | SPS |
  |---|---|---|---|---|---|
  | plain (no features, no teacher) | .106 | .079 | .062 | 68% | ~224K |
  | plain + teacher 0.3 | .127 | .079 | .080 | 70% | ~220K |
  | features | .660 | .466 | .440 | 92% | ~165K |
  | features + teacher 0.3 | .654 | .471 | .361 | 93% | ~165K |
  (magnets: plain .096, plain+teach .123, feat .592, feat+teach .611 vs Maia 1100.) "Natural leads converted" =
  lead_win / lead_n from the run log at the end of training (games not started from the teacher pool).
  - The per-move features are the lever: .11 -> .66 vs Maia 1100 at 200M steps. A 256x2 net at 200M steps is at
    the Maia level of the 1024x2 states_cont net after 14B steps (.645 / .487 / .391).
  - The teacher changes little here: within noise on Maia, +1-2 points of converted leads. With the features the
    net already converts 92% of its leads, so thrown wins are no longer the main leak.
  - Stockfish 1320 and local head-to-head / endgame probes for the feature arms: running.
- 2026-11-17 local checks on the two feature arms (256x2, 200M; `arms256/`):
  - Head-to-head (`match.py`, greedy, 200 openings x 2 colors): features+teacher scores .486 +- .025 against
    features only (146 wins, 97 draws, 157 losses). No difference.
  - Endgame probe, wins out of 60 vs full Stockfish (features only / features+teacher / for reference the 1024
    states_cont magnet): KQK 60 / 60 / 60; KRK 49 (11 threefold) / 60 / 60; KRRK 56 / 60 / 59; KQPP v KPP 59 / 58 /
    54; KQR v KR 58 / 56 / 55; KRPP v KPP 51 / 48 / 46; KQNPP v KRPP 45 (10 mated) / 41 (15 mated) / 44 (9 mated).
    Mate in 1 is taken 100% of the time by both (the "mates" feature bit).
  - So the teacher fixes the elementary mates it practises (KRK, KRRK) and nothing else yet. The leak that is
    left, with or without it: getting checkmated while ahead with pieces still on (10-15 of 60). The lookahead
    only follows captures, so it cannot see a quiet mating reply. The planned "allows mate in 1" bit targets
    exactly this; "repeats a position" targets the threefolds.
- 2026-11-17 Stockfish 1320 (box, 1 s/move, 200 games): a256_feat_teach policy .359 = ~1220. So the 256x2 net at
  200M steps is at ~1210-1220 on both honest anchors (Maia 1100 .654), level with the 1024x2 states_cont net after
  14B steps (.340, ~1205) and with rl_best (.363). Each Stockfish eval takes ~40 min on this box (11 cores, busy host).
- 2026-11-17 main run queued on the box: tmux `main1` waits for the arm scoring, then runs
  `/workspace/chess_teach/main1.sh` (1024x2 from scratch, features + teacher 0.3, 6B steps, lr 0.00208645 -> 0.4x,
  entropy 0.0058 -> 0.1x, magnet 0.003 / tau 0.005, pause + score every ~1.05B: Maia 1100 200 games, SF 1320 100
  games, policy and magnet; 200 SF games at the end). Changed from the earlier plan (continue the 1200 net)
  because a fresh net with features relearns that level in ~200M steps.
- 2026-11-17 06:50 UTC: **main1 started** (tmux `main1` on the box, ~112K SPS alone). The first launch failed at once
  (bench.sh had no default binary; fixed). The remaining arm scoring (SF for feat_base, the Maia curve) was stopped
  to free the box; redo later if wanted.
- 2026-11-17: Stockfish move time matters, so no shortcut: the same checkpoint (a256_feat_teach) scores .359 vs
  SF 1320 at 1 s/move on a quiet box and .443 at 50 ms/move beside training (200 games each). Honest Stockfish
  numbers need 1 s/move and nothing else on the CPU; a 200-game eval is ~40 min here, 100 games ~20 min.
  Training drops from ~112K to ~98K SPS while an eval shares the CPU.
- 2026-11-17 Felix: at the pauses score policy and magnet on Maia 1100 only. Done without touching the running
  loop: `/workspace/chess_eval/NO_SF` exists, and run_eval.sh skips Stockfish while it does. So each ~1.05B pause
  of main1 = Maia 1100 (200 games) for policy and magnet, a few minutes. Remove NO_SF before the end of the run (or
  run Stockfish 1320 by hand on the final policy and magnet) to get the honest final number.
- 2026-11-17 ~08:20 UTC, main1 at 607M (dashboard only, no strength number yet). Entropy 1.55 -> 0.70 (43M) ->
  0.37, falling smoothly; kl 0.007, clipfrac 0.011. Draws 71% at start -> 10-16%, games ~80 plies. Natural leads
  (+5 pawns held 4 plies) converted: 85% at 43M, 93% at 130M, 93-95% since (256 arms ended at 92-93%, plain
  arms 68-70%), so flat since ~200M. Teacher practice starts win ~78% (teach_win/teach_n .23/.29). Teacher
  table at 550M: kbbk is the one bank it can't do (p .06, fast .21); lead5_light/heavy have the most failure
  mass (convert 89%/83%) and get the most weight (.14 each); kqk/krk/krrk solved (fast ~1.0).
- 2026-11-17 14:14 UTC, main1 at 3.0B. Pause evals vs top-move Maia 1100 (~206 games each, from varied openings):
  1.32B policy .738 (~1280) / magnet .816 (~1360); 2.63B policy .827 (~1370) / magnet .829 (~1375). Previous best
  pure self-play (states_cont magnet) was .645 (~1200). One anchor only and .83 is near the top of its useful
  range: Stockfish 1320 at 1 s/move on a quiet box is needed before calling it ~1370. Dashboard: entropy .19,
  kl .003, draws ~14%. Teacher at 2.9B: kbbk now solved (fast 1.00, was .21 at 550M); lead5_heavy is still the
  weak spot (natural conversion 86.5%, most failure mass, prob .135).
- 2026-11-17 16:00 UTC, main1 Maia curve beside training (/workspace/chess_eval/curve.sh, tags main1c_*, ~206 games,
  ~21 s each). Maia 1100 magnet / policy: 262M .635/.673 (~1195/~1225), 524M .757/.686, 786M .745/.713,
  1.05B .817/.690, 1.32B .816/.738, 2.63B .829/.827, 3.41B .853/.833 (magnet ~1405). Not capped at 200M: +100 Elo
  by 1B, then only ~+50 over the next 2.4B (slowing, roughly log-shaped). Maia 1500 at 2.62B: magnet .700 / policy
  .724 = "~1650", the same ~+270 inflation Maia 1500 showed on control2, so it backs ~1375 rather than contradicting
  it. Gotcha: run eval on the box with the venv NCCL lib on LD_LIBRARY_PATH or it dies at once (empty results).
- 2026-11-17 16:40 UTC. 256-arm Maia 1100 curves (policy / magnet; the 256 arms ran 200M only with LR annealed over
  200M): feat_teach 52M .42/.10, 105M .61/.41, 157M .59/.59, 200M .65/.61; feat_base 52M .50/.09, 105M .58/.45,
  157M .67/.58, 200M .66/.59. main1 (1024) at 262M = .67/.64: at equal steps the 1024 net is no better than 256;
  the 1024 run's extra ~+200 came from more steps. main1 3.94B pause: policy .815 / magnet .850 = flat vs 3.41B
  (.833/.853): plateau at ~1400 (Maia 1100) since ~3B.
- Failure fingerprinting started. chess.h: CHESS_GAMES_OUT=<file> makes Maia-mode eval append one line per game
  (learner color, result, start FEN, UCI moves); built on the box as /workspace/PufferLib/puffer_chess_pgn (training
  binary untouched). 1000 games each, main1 3.41B magnet: vs Maia 1100 .837 (96 L, 137 D), vs Maia 1500 .712 (202 L,
  177 D); files in failures/. failure_scan.py: Stockfish (60K nodes) win% per position, the learner's costliest move,
  tagged by what the net's own lookahead features said about it and about SF's move.
- 2026-11-17 17:30 UTC, **failure fingerprint of main1 3.41B magnet** (failure_scan.py + failure_drill.py; scans in
  failures/scan/). Points lost (loss = 1, draw = .5): 164.5 vs Maia 1100, 290.5 vs Maia 1500, 455 total:

  | category (decisive move = biggest SF win-% drop >= 15) | pts | share |
  |---|---|---|
  | quiet tactic: refutation is a quiet move, our capture lookahead saw nothing | 109 | 24% |
  | fair draw (never >= 85% win): mostly repetition in level positions | 95 | 21% |
  | check tactic: refutation is a check | 61.5 | 14% |
  | allowed mate (12 losses + more were mate in 1) | 58.5 | 13% |
  | obs flagged the material loss, net played it anyway (in ~29/52 SF's move lost too) | 45 | 10% |
  | capture tactic our quiescence missed | 38 | 8% |
  | gradual drift, missed mate, ignored flagged gain | 48 | 10% |

  Decisive mistakes: middlegame (14-25 pieces) ~65%, opening ~21%, endgame ~20%. Candidate features scored on the
  played move vs SF's move (fires on played, not on best = would have told them apart): "allows mate in 1" 27 pts
  (all of it discriminating); "check-extended quiescence" (opponent's first reply may be a check, then captures)
  loses >= 200 more than plain quiescence: 39.5 of the check tactics + 33.5 of the allowed mates, all
  discriminating; "some reply leaves 2 pieces en prise" fires on SF's move too (not useful). Quiet tactics: no cheap
  feature found (46.5 pts no piece en prise after the refutation = positional/deep; 37.5 one piece en prise
  (trapped/pinned); 25 double attack).
- Repetition: 239 of 314 draws end in threefold, but SF on the last position: 179 level, 43 we were worse, only 14
  we were >= 80% winning. A "repeats a position" bit would buy ~7 of 455 points: dropped. Stalemates were mostly Maia
  stalemating us from a won position (lucky saves), not ours.
- Ceiling estimate for check-extended + mate-in-1 bits: ~80 of 455 points if the net used them perfectly: ~.84 ->
  ~.87 vs Maia 1100, ~.71 -> ~.76 vs Maia 1500 (+30-60 Elo). The biggest buckets (quiet tactics, level draws) are
  general strength that obs bits this cheap won't fix.
- 2026-11-17 17:30 UTC, **-DCHESS_MOVE_FEATS2** (chess_tactics.h, chess.h, chess_decoder.cu; all under the flag, off =
  byte-identical code paths): two more per-move bits, f[7] allows mate in 1, f[8] check-led loss (a quiet opponent
  check + our best evasion + plain quiescence leaves us >= 200 worse than the plain swing; mates count). Slot code
  bits 5/6, empty test 127, CHESS_MF 9, mf_w stored as 12 floats (16-byte rule). Verified with
  ocean/chess/tools/mf2_probe.c: on the 401 decisive mistakes the C bits fire on the played move and not on SF's
  move for 78 of 455 lost points (check tactics 37/61.5, allowed mates 33.5/58.5; mate1 alone 26), same as the
  Python drill; bits 0..6 checksum identical with and without the flag on 16.7K game positions; cost 89 -> 210
  us/position; new-bit rates 0.5% (mate1) and 3% (check-led loss) of moves. Box binary puffer_chess_teach2.
- A/B queued on the box (tmux arms2, /workspace/chess_teach/arms2.sh): waits for main1.sh to exit, then 256x2
  teacher 0.3 200M: feat2 seed 73, feat2 seed 74, feat seed 74 (feat seed 73 = a256_feat_teach). Each scored on
  Maia 1100 x 1000 games (policy + magnet) with games logged; then failure_scan on them: pass = the allowed-mate and
  check-tactic shares shrink, and the score doesn't drop. main1 final Stockfish 1320 (NO_SF skips it at the end
  too): run by hand on a quiet box after the arms.
- 2026-11-17 19:25 UTC: Felix's second box (Vast 60996371, ssh3.vast.ai -p 36663, same host machine, 5060 Ti, was
  idle; its /workspace/pufferlib + sweeps belong to earlier work and were left alone) now runs the A/B instead of
  box 1: chess setup copied box1 -> box2 (/workspace/PufferLib binaries, chess_teach, chess_eval, teacher_banks;
  env in /workspace/chess_env.sh). Box 1's queued arms2 waiter was stopped (PID). Baseline a256_feat_teach,
  1000 games Maia 1100: policy .643, magnet .631. feat2 256x2 runs at ~100K SPS (feat: ~167K; env 50% of the epoch).
- 2026-11-17 20:00 UTC: main1 5.24B pause: policy .837 / magnet .875 (200 games). Same 1000-game test as 3.41B:
  magnet .856 (3.41B: .837): ~+25 Elo (~1385 -> ~1410, Maia 1100 only) over 1.8B steps. Felix: kill it if still
  flat. Stopped by PID at 5.24B (checkpoint + state/ kept); box 1 now runs arms3 (feat2 / feat, seed 75).
  Honest caveat Felix raised: we never ran a 256 past 200M (and the 256 arms' LR was annealed by 200M), so "1024
  paid off" is confounded with steps; a long 256 run is the test.
- **A/B seed 73 (1000 games Maia 1100):** feat .643 / magnet .631; **feat2 .708 / magnet .682** (~+50 Elo).
- Seed-73 A/B failure scan (policy, 1000 games each, points lost per 1000 games, feat -> feat2): allowed mate 50.0 ->
  21.7, check tactic 31.5 -> 23.7, capture tactic 29.0 -> 19.2, obs-flagged loss ignored 48.0 -> 30.1, quiet tactic
  99.9 -> 98.7 (unchanged), fair draws 70.9 -> 72.6 (unchanged). Total 357.5 -> 295.5. The gain sits in the targeted
  buckets (mates/checks) and the untargeted ones did not move: the feature does what it was built for.
  feat2 curve (200 games, policy/magnet): 52M .50/.08, 105M .63/.45, 157M .71/.64 (feat: .42/.10, .61/.41, .59/.59).
- **A/B done, 3 seeds (Maia 1100, 1000 games each; policy / magnet):** feat s73 .643/.631, s74 .670/.651, s75
  .671/.619 (mean .661/.634); **feat2 s73 .708/.682, s74 .704/.688, s75 .709/.690 (mean .707/.686)**: every feat2 seed
  beats every feat seed, ~+35-40 Elo. feat2 is the new default. feat2 s73 vs Maia 1500 (1000 games): .517 / .518.
  (Felix: one seed is enough for these A/Bs; feat2's 3 seeds spread only +-.005, so single seeds are trustworthy here.)
- 2026-11-17 20:45 UTC box 2: **long256** (/workspace/chess_teach/long256.sh): feat2 256x2 teacher 0.3 seed 73, 2B
  steps, LR/entropy annealed over 2B, checkpoint every ~131M: the size-vs-steps test against main1's curve.
- 2026-11-17 21:05 UTC box 1: **failure-state teacher group** ("in-game failure states", Felix's original teacher
  idea). chess_teacher.h: a bank line "name path stm" = the side to move is the favored side and a practice game
  counts as success unless it loses (blunder positions are mostly level, so the >= 300 cp lead rule would never
  score them); chess.h teach_stm. test_teacher passes with and without FEATS2. Bank failures/banks/blunder.txt: 955
  distinct positions just before the decisive mistake (SF drop >= 15 win-%) from all scans so far (main1 3.41B vs
  Maia 1100/1500, 256 feat/feat2 s73), 50-move clock reset. Run a256_blunder_s73 (binary puffer_chess_teach2b,
  folder /workspace/chess_teachb so the normal groups file is unchanged; seed 74 skipped by a guard in its bench.sh),
  scored on Maia 1100 + 1500. Caveat: the bank comes from games vs Maia from the same 1000 eval openings, so a gain
  vs Maia could partly be practice on positions this eval produces; check against SF or other openings if it wins.
- **Blunder group, seed 73 (1000 games each), vs feat2 s73:** Maia 1100 policy .721 (.708) / magnet .661 (.682);
  Maia 1500 policy .541 (.517) / magnet .502 (.518). Policy ~+15 Elo, magnet ~-15: each ~1 SE, opposite signs:
  **neutral at 200M**. Teacher: blunder group got 11.2K practice games (~5% of teacher games, prob .096 at the end,
  2nd highest), p .62 (fast .74): it is learning those positions, but 955 positions replayed ~12x each. Not the
  default for now; keep collecting failure positions from every eval scan (cheap) and retest with a much bigger bank
  in a long run once feat2 plateaus. Side note from this run's table: kbbk p .004 at 256/200M (main1 solved it by
  ~2.9B), kqkr .48.
- 2026-11-17 21:55 UTC box 1: main1 5.24B Stockfish 1320 at 1 s/move, 200 games, magnet then policy, quiet box
  (/workspace/chess_eval/sf_main1.sh; NO_SF removed). Tags main1_5243928576[_magnet]_sf_quiet.
- 2026-11-17 22:20 UTC, **eval cadence check** (Felix asked if evals run memory the way training does). Self-play is
  one env step per ply (both agents observe every ply); Maia mode spends two steps per Maia move (a no-op left over
  from pick+place actions), so the net's recurrent state sees each Maia move twice. chess.h: CHESS_MAIA_ONE_STEP=1
  (runtime env var, default off) makes Maia move in one step. feat2 s73, 1000 games Maia 1100: two-step .709 / magnet
  .681, one-step .705 / .681: no difference, so all Maia numbers so far stand. (Box 2 now has a source tree in
  /workspace/PufferLib: src, vendor, raylib, ocean/chess, ocean/retro/curriculum_teacher.h, build.sh.)
- 2026-11-17 22:40 UTC, **main1 honest number** (5.24B, quiet box, Stockfish 16 UCI_Elo 1320 at 1 s/move, 200 games):
  magnet .607 (~1395), policy .567 (~1365). With Maia 1100 (magnet .856 / 1000 games = ~1410): **main1 = ~1395
  magnet, ~1365 policy**, +-~25. Previous best pure self-play ~1200; level with the SF-BC 2048 model (~1410).
- 2026-11-17 22:40 UTC box 1: **main2** = main1 + FEATS2 (/workspace/chess_teach/main2.sh): 1024x2 from scratch,
  teacher 0.3 (default groups), 3B steps, LR/entropy annealed over 3B, checkpoint every ~262M, no pauses (score
  checkpoints beside training, Maia 1100 x 1000). ~89K SPS at start: ~9.4 h.
- 2026-11-17 23:20 UTC, **long256 curve** (256 feat2, 2B schedule; Maia 1100, 1000 games, policy / magnet): 131M
  .633/.531, 262M .717/.680, 393M .708/.721, 524M .751/.764, 655M .778/.785 (~1325). main1 (1024 feat, 200 games)
  at the same steps: 262M .673/.635, 524M .686/.757, 786M .713/.745. So 256+feat2 >= 1024+feat up to ~650M, still
  climbing. Not yet a clean size test (feat2 itself is worth ~+40): main2 (1024 feat2) gives the same-recipe curve.
- 2026-11-18 04:55 UTC. **long256 done** (2B, 03:23 UTC): magnet .808 @1.05B, .834 @1.31B, .829 @1.57B, best .842
  @1.84B, final .824 (~1370); flat from ~1.2B. **main2** (1024 feat2) at 1.6B, Maia 1100 1000 games policy/magnet:
  262M .728/.737, 524M .765/.789, 786M .817/.810, 1.05B .825/.824, 1.31B .838/.851, 1.57B .847/.863 (~1420).
  Size vs steps, same recipe (feat2): 1024 ahead of 256 by ~.02-.03 at every step count, and the 256 flattens around
  .82-.84 while main2 is still rising. main2 at 1.57B already matches main1 at 5.24B (.856). Box 2 idle since 03:23.
- 2026-11-18 05:20 UTC. **main2 1.57B magnet leak profile** (scan on box 2, SF 15K nodes; per 1000 games Maia 1100,
  main1 3.41B -> main2): allowed mate 26.8 -> 12.4, check tactic 21.4 -> 6.5, level draws 38.7 -> 40.2, quiet
  tactic 37.7 -> 39.7, capture tactic 12.4 -> 13.9; total 163 -> 137. FEATS2 holds at 1024; what is left is level
  draws + quiet tactics (~2/3 of losses).
- fork_probe.py (cheap fork bit: after our move, a safe quiet opponent move attacking >= 2 of our pieces): over all
  scans it would tell played from SF's move on only 20.5 of 346 quiet-tactic points (fires on SF's move for 13.5).
  Not worth building; quiet tactics need real depth (3-ply), too costly as an env feature.
- Box 2 queue: a256_feat2_noteach (teacher off, ends ~05:45) -> a256_feat2_mag01 (magnet_coef 0.01 vs 0.003,
  /workspace/chess_teach/arms6.sh, auto-starts when arms5 exits; later CLI flags override earlier ones). Both scored
  by autoscore.sh. Felix: no idle boxes; status in plain per-box lines.
- 2026-11-18 05:55 UTC. **Teacher off** (feat2 256 s73 200M, Maia 1100 1000 games): policy .716 / magnet .680 vs
  teacher 0.3 .708 / .682 (s74 .704/.688, s75 .709/.690): no difference. The teacher fixes endgame banks but does
  not move strength at 256/200M. Box 2 queue now: mag01 (running, ~06:30) -> mag001 (magnet_coef 0.001, arms7.sh).
- 2026-11-18 06:30 UTC (session restart; box-side queues survived, local watchers re-armed). main2 1.84B: .845 /
  magnet .860 (1.57B .847/.863): levelling at ~1415. mag01 early curve is slower (105M .566/.261 vs base .63/.45).
  Box 2 queue: mag01 (running) -> mag001 (arms7) -> tau02 (arms8: magnet_tau 0.02 vs 0.005), each auto-scored.
  Box 1: main2 (ends ~09:40) -> sf_main2.sh (waits for main2's autoscore "done", then SF1320 1 s/move 200 games on
  the final magnet + policy with the FEATS2 binary linked as puffer_chess, link restored after).
- 2026-11-18 06:35 UTC. **magnet_coef 0.01** (feat2 256 s73 200M, Maia 1100 1000 games): policy .683 / magnet .618 vs
  base 0.003 .708 / .682. Worse, and slower all the way (157M .653/.500 vs base .71/.64): a 3x stronger pull holds
  the policy back at this horizon. mag001 (0.001) running now.
- 2026-11-18 07:20 UTC. **magnet_coef 0.001**: policy .715 / magnet .692 (base 0.003 .708/.682, 0.01 .683/.618),
  and faster early (105M .661/.518 vs .63/.45). Weaker pull >= base > strong (0.001 vs 0.003 within noise).
  Queue: tau02 (running) -> mag0 (coef 0, EMA file still kept: pull vs averaging).
- 2026-11-18 08:00 UTC. **magnet_tau 0.02** (EMA 4x faster, coef 0.003): .728 / .722, the best 256 short run
  (base tau 0.005 .708/.682). Magnet copy gains most. Felix redirect: stop small side tests, drop Stockfish (Maia 1100 only,
  that is what the other side reports), one model line, find max practical size (2048+) on one bigger box, sweep a bit,
  then the teacher (in-game failure states harvested mid-training, still NOT built). sf_main2 cancelled; box 2 backed up
  to D: chess_box2_60996371_20261118 and destroyed. main2 2.36B: .856 / .882 (still climbing).
- 2026-11-18 09:47 UTC. **main2 final (3B)**: policy .877 / magnet .874 vs Maia 1100 (~1440 / ~1435). Magnet peaked
  .882 at 2.36B then .877/.867/.874: plateau ~1435-1450. Policy still rose (.845 -> .877 over the last 0.8B).
- 2026-11-18 09:50 UTC. **main3 started on box 1**: 2048x2 (36.5M params), main2 recipe + magnet_tau 0.02, 3B.
  54K SPS (main2 1024: 81K), 13.5 GB of 16 GB: 2048 is the largest size this 5060 Ti fits; 3072+ needs a bigger box.
  3B ~15.5 h. Checkpoint every 262M (~1.35 h), autoscored (Maia 1100, 1000 games).
- 2026-11-18 10:55 UTC. **Moved to one RTX 5090** (Vast 61230905, 48 cores, $0.667/h); both 5060 Ti boxes destroyed,
  backups on D:. Size probe (40 env threads, 2048 agents, feat2): 2048x2 36.5M params 185K SPS 17 GB; 3072x2 73.5M
  93K SPS 19 GB; 4096x2 123M 52K SPS 21 GB. Now GPU-bound (train ~half of each epoch, env 5-18%).
  **main3 restarted here**: 2048x2, main2 recipe + magnet_tau 0.02, 3B (~5 h), autoscored per 262M checkpoint.
  main2 final re-scored on the 5090 as a cross-box sanity check.
- 2026-11-18 12:50 UTC. **Swing harvest** (in-game failure states, online): CHESS_TEACH_SWING=1 adds a "swing" ring
  (stm mode) to the teacher. Each natural self-play game: first stable material swing (level |d| <= 150, then one side
  >= 300 worse for 4 plies), last position before it with that side to move; banked if that side then doesn't win.
  Code: chess_teacher.h (chess_track_swing, cteach_swing), chess.h hooks; test ocean/chess/tools/swing_test.c (queen
  blunder, hanging knight, knight-for-pawn caught; trades not; 2000 random games, all FENs load, side to move right).
  Binary puffer_chess_teach2s on the 5090 (feat2 + swing; off unless the env var is set). Smoke test 256, 6M steps:
  ring full (4096), 8136 failures, teacher picks it 13.4%, practice p .63.
- 2026-11-18 13:20 UTC. **main3 (2048) stopped at 1.57B**: .841/.850 vs main2 (1024) .847/.863 at the same steps
  (1.31B .820/.829 vs .838/.851). Size does not help per step; stay at 1024 for real runs.
  First 5090 (61230905) went "exited" right after the kill and could not restart (GPU re-rented); replaced by 5090
  61256274 (ssh -p 12384 root@192.234.50.233), set up from local source + D: backup.
- 2026-11-18 13:45 UTC. Swing A/B running (s256_base, s256_swing; 256x2, 200M, tau 0.02). **Hyper sweep queued**
  behind it: /workspace/chess_sweep (config = sweep256_chess.ini here), 256x2 x 200M, 18 swept dims around the main
  recipe, scored in-trial by 600 games vs top-move Maia 1100 from the training opening bank, max_runs 500.
- 2026-11-18 14:11 UTC. **Swing A/B (256, 200M, one seed)**: base .711 / .717, swing .700 / .710 -> no gain (within
  noise, slightly worse). Teacher: 63K swing failures harvested, picked 13%, but practice success only p .26: most
  harvested positions are already lost for the side to move (the real mistake came earlier, or the swing is forced).
  Possible fix later: harvest 2-6 plies further back / only where the side to move still holds. Sweep runs without it.
- 2026-11-18 14:47 UTC. Sweep try 1: run 0 (base settings) .729, run 1 .669, run 2 .640, runs 3-5 failed at start
  (4096+ agents: cudaMalloc fails in 32 GB). Restarted with total_agents 1024-2048 (history of try 1 kept as
  sweep.history.try1).
- 2026-11-18 22:40 UTC. Sweep fix (Felix: scoring only the policy wastes the sweep). The bot-ladder eval scored only
  the final policy. New `[selfplay] eval_bot_magnet` (src/pufferl.cu, audit in reports/core_changes_audit.md): also
  plays <final>.magnet, trial score = max(policy, magnet). Binary puffer_chess_teach2sm. The finished trials' magnet
  copies are rescored on the box (rescore.sh, same protocol; check: trial 0 policy .719 here vs .746 in-trial, within
  noise) and finalize.sh rewrites sweep.history to max(policy, magnet) and resumes the sweep with the new binary
  (sweep1024_chess.ini has eval_bot_magnet = 1). Originals kept as sweep.history.policy_only / sweep.log.policy_only.

## 2026-11-19 drop-ladder teacher test (5070 Ti, trial 25 hypers, 1024x2, 400M, 1000 games vs Maia 1100)
Drop ladder (CHESS_TEACH_SWING=1, chess_teacher.h): each side's first stable >= 2-point material drop per natural
game (from any level, incl. a bled lead); positions 2/4/6/10/16 plies before it, losing side to move, one ring per
depth (stm: success = not lose). Rings filled (30-70K plays per depth, held 37-66%).
| steps | drop80 (ladder, tp .8) | base30 (old teacher, tp .3) | drop30 (ladder, tp .3) |
| 100M | .643 / .639 | .654 / .650 | .658 / .645 |
| 200M | .708 / .702 | .719 / .728 | .683 / .715 |
| 300M | .719 / .716 | .750 / .727 | .723 / .744 |
| 400M | .726 / .726 | .760 / .757 | .746 / .755 |
(policy / magnet.) Result: the ladder does not help at .3 (tie) and hurts at .8 (flat after 200M). base30 = best 400M
run so far (~1300). Sweep resumed after (completed=25, next trial 28).
- 2026-11-19 07:43 UTC **cont_drop30**: main2 final (3B, .877 / .874) continued 300M with the drop ladder at tp .3
  (lr 0.0008 -> 0.4x, ent 0.00058 const, fresh optimizer). Maia 1100, 1000 games, policy / magnet:
  100M .872 / .889, 200M .862 / .873, 300M .884 / .883. Within noise (+-.010) of the start: the ladder does not lift
  the plateau either. main2 held its own drop positions 32% (-2), 45% (-4), 41% (-6), 39% (-10), 53% (-16).
- 2026-11-19 **main2 on Lichess** (felix_pufferlib, local tmux lichessbot, engine lichess_engine/engine2.py + 2-ply
  material check of the policy's top 5). Port bug found on the way: the bf16 param buffer aligns tensors to 8
  elements, feat2's 12-float mf_w leaves a 4-element gap, so mf_u and the GRU live 4 floats after their packed offsets
  (every feat2 run since 11-17; Muon sees shifted GRU views; fix = CHESS_MF_STORE 16, not applied yet). Port check
  with aligned offsets: first-move agreement with native eval games 87% (packed: 4%); 100 games: policy only vs Maia
  1100 .875 (native .877), 2-ply vs Maia 1900 .820 (policy only .659): ~+150 Elo, ~2160 with Maia 1900 = 1900.
- 2026-11-19 **fix30** (CHESS_MF_STORE 16, bf16 alignment fixed; otherwise = base30, seed 73): 100M .666 / .641,
  200M .703 / .718, 300M .729 / .762, 400M .752 / .739 (policy / magnet) vs base30 .654 / .728 / .750 / .760 (best
  of the two). Within noise at every step: the 4-element shift cost nothing measurable. Fixed build kept for new runs
  (correct layout); the sweep stays on its binary for comparability.
- 2026-11-19 Lichess, 20 rated bot games with main2 + 2-ply: W4 D3 L13 vs bots 1500-2350 (blitz 1416 -> 1434).
  Stockfish pass: no clock trouble (bot moves near-instantly, clock stays >= start), 15/20 games have one move that
  drops >= 2.5 pawns at moves 13-25 (incl. thrown wins), 4 losses are slow grinds vs engines. Bot stopped 06:34 EST.
- 2026-11-19 **Maia 1900 re-score + teacher controls** (all main2 + 300M, lr .0008, ent .00058, seed 73; vs top-move
  Maia 1900, 1000 games, policy / magnet; main2 itself .659 / .640):
  cont_off (teacher 0) .681 / .674; cont_drop30 (drop ladder + lead + endgame banks, .3) .681 / .681;
  cont_harvest (.3, ONLY real-opponent collapse banks: 6,431 positions 2-16 plies before 1,434 collapses in 4,026
  main2-vs-Maia-1900 games, stm) .668 / .657. The low-LR continuation itself is the gain (Maia 1100 hid it:
  .877 -> .884); no teacher adds anything. In self-play main2 scores ~.46-.68 from its own collapse positions as the
  side to move: the positions are not lost; what is missing is an opponent that punishes them.
  Scratch 400M vs Maia 1900: base30 .499 / .510, drop30 .487 / .500.
- 2026-11-19 MCTS (mcts.py, PUCT 64 sims, paired critic leaf, 0.35 s/move CPU) vs Maia 1900, 100 games: prior_t 4
  .815, prior_t 1 .715; 2-ply check .820. Critic vs Stockfish on 2,006 Lichess positions: Spearman .88, sign .93
  (|SF| >= 100). Lichess blunders (26, Stockfish d16): 17 from winning positions, 12 positional, 3 within 1-3 plies.
