# Data not in the repo

Only scripts, configs, notes and small result files are committed. Large position sets and game logs (over 100 KB) stay
off the repo; regenerate them or ask for them.

- `resources/chess/fens.txt` (committed, 5 MB): the 81K opening bank every chess run starts 90% of its games from
  (`ocean/chess/tools/gen_states.py`: Lichess openings + Maia / Stockfish continuations, Stockfish-filtered, eval
  openings held out). Required: `env.fen_curric_pct` reads it.
- Endgame teacher banks (`teacher_banks/*.txt`, 13 x 20K positions): `ocean/chess/tools/gen_teacher_banks.py`. No
  measurable effect on Maia scores (NOTES.md 2026-11-19), so not needed to reproduce main2.
- Real-opponent collapse banks (6,431 positions) and main2-vs-Maia-1900 game logs: `tools/harvest.sh` +
  `tools/collapse_bank.py`. Also neutral.
- Failure scans, Stockfish baselines and probe dumps (`failures/`, `local_sf/`, `probe/*.json`): rerun with the tools
  next to them.
