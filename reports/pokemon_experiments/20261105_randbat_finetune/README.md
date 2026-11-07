# Gen 1 random battles: fine-tune of the ladder model (2026-11-05)

Goal (user): one training run to make our best Gen 1 OU model good at Gen 1 random battles (Showdown gen1randombattle).

## Result
The fine-tune beats the model it started from (ladder-run 2B, `checkpoints/pokemon_final/20261028_ladder_2B.bin`)
**70.0% (same team) / 70.2% (different teams)** in randbats on 2,000 held-out teams, 8-10K games per test. Through
real Showdown (local server) it beats a max-damage bot 181-19 (90.5%); the starting model scores 168-32 (84%).
Kept: `checkpoints/pokemon_final/20261105_randbat_1B.bin` (md5 8c93a254438cb684a68bf56ebf99240a; its run config is
`checkpoints/pokemon_final/20261105_randbat_1B.config.ini` and `final/config.ini`). Live on the ladder bot as
`LADDER_CK_RANDOM` (chess box, /workspace/showdown_ladder; used whenever `LADDER_FORMAT` is a random format).

| Steps | Same team | Different teams |
|---|---|---|
| 26M (smoke) | 55.7 | 56.0 |
| 100M | 62.8 | 62.5 |
| 200M | 65.9 | 65.7 |
| 300M | 67.7 | 66.8 |
| 420M | 69.4 | 68.8 |
| 520M | 69.0 | 69.3 |
| 630M | 70.5 | 69.3 |
| 730M | 71.1 | 69.3 |
| 840M | 70.5 | 69.4 |
| 940M | 70.3 | 70.0 |
| 1B (kept) | 70.0 | 70.2 |

Control: the starting model against itself scores 50.6 / 49.9, so the test has no side bias. Plateau from ~420M: more
of the same training is unlikely to add much.

## Setup
- Teams: `gen_teams.js` calls Showdown's own generator (pokemon-showdown 0.11.11, `Teams.generate('gen1randombattle')`):
  32,000 training teams (`randbat_teams.tsv`) and 2,000 separate eval teams, in the trainer-file format, levels as
  generated (mostly 68-90), Showdown order (first Pokemon leads).
- Env: trainer mode (from the Gen 1 trainer tournament), Showdown-compatible engine build. New `env.trainer_perfect = 2`:
  Showdown stats plus full PP (PP Ups), as Showdown gives every set. `PK_MAX_TRAINERS` 512 -> 32768.
- Not matched: Showdown gives ~22% of sets attack DV 1 / attack stat experience 0 (special attackers); our env gives
  every set attack DV 15. It only matters for confusion self-hits.
- Training (`make_config.py`): the ladder run's config, pure self-play on two different random teams, 1B steps,
  ~140K SPS on an RTX 5060 Ti (temp Vast box 59304614, destroyed after this run). replay_ratio 1.
- Eval (`eval_h2h.sh`, `watch_eval.sh`): `./puffer match` vs the starting model on the held-out teams, mirror (both
  sides the same team) and random (two teams); results in `h2h.txt`.

## Showdown bridge fixes found on the way
- `policy_server.c`: room ids up to 255 characters. Hidden battles have 68-character ids; the old 63-character limit
  cut them, the answer never matched, and the bot lost on time (one randbat ladder game; no OU game was affected).
- `tracker.ts`: our own Pokemon's level from the request (was always 100).
- `client.ts`: `/utm null` for random formats; `--status-file` for a live status line.
