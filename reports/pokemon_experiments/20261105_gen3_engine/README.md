# Gen 3 engine: poke-engine vs Pokémon Showdown (2026-11-05)

Goal (user): pick the next generation after Gen 1. Gen 3 OU is the most played old gen on Showdown (Smogon stats,
October 2026: gen3ou 198K battles, gen4ou 55K, gen1ou 13K). pkmn/engine (Zig) only has Gen 1, so Gen 3 means
poke-engine (Rust; Gen 3 has its own code in src/gen3, separate from genx = Gens 4-9). This is the Gen 9 differential
test (`../20261029_gen9_engine`) ported to Gen 3 and run before any training.

## Result
1,000 random positions from `gen3randombattle` games (random choices), 200 Showdown samples each, local, ~80 minutes on
4 cores. 9 crashed the converter (Wish, fixed since), leaving 991. Two ways to score them:
- Full turns: the 941 positions whose first Showdown sample did not end in a KO. Agree = mean HP of every Pokémon within
  3% of max HP, actives/statuses and boosts within 0.08 total variation distance (the Gen 9 test's thresholds; Gen 9
  reached 997/1,000).
- KO-aware, all 991: in Gen 3 singles a KO sends the replacement in mid-turn, before end-of-turn effects, so Showdown's
  KO samples stop there while poke-engine plays the turn out (Leftovers, poison). This compares the chance of each KO
  with the actives and statuses, and HP and boosts only over the outcomes without a KO (`KO_SPLIT=1 rescore.js`).

| Engine (fork branch `gen3-diff`) | Full turns (of 941) | KO-aware (of 991) |
|---|---|---|
| Fork as it was (gen9-merged + test binary, a0b3048) | 856 (91.0%) | 905 (91.3%) |
| + 7 fixes (5cd7740) | 925 (98.3%) | 974 (98.3%) |
| + 7 more fixes (a1b1314) | **932 (99.0%)** | **984 (99.3%)** |

## Engine fixes (fork FelixAllistar/poke-engine, branch `gen3-diff`)
One commit each, with a test (`tests/test_gen3.rs`, 44 pass; genx test_battle_mechanics 751 pass after the two shared
fixes):
- cfca213 Leftovers heals before burn/poison damage (genx had this fix; Gen 3's own end-of-turn code did not).
- 5aba8b7 Spikes: 1/8, 1/6, 1/4 per layer count (was layers/8).
- c546794 test only: `Instruction` is still 6 bytes in gens 1-3 (our Items-as-u16 refactor set 8 for all gens).
- 9b54331 Speed Boost does not activate on the turn of switching in.
- 036b313 Rest fails at full HP.
- 696a8ff Toxic from a Poison-type user can miss (the never-miss rule is Gen 6+; genx also applies it to Gens 4-5).
- e84211b Rock Head prevents recoil (was in the enum, no effect).
- 5cd7740 Synchronize (was in the enum, no effect); Toxic comes back as regular poison.
- 8a381e4 End-of-turn items (Leftovers, pinch berries, Sitrus) resolve before status damage; in Gen 3 pinch berries
  only activate at end of turn.
- 81dfa5e Guts boosts physical moves only, and not a Pokémon that wakes up or thaws that turn.
- ebd320d Crit stages: Focus Energy, Scope Lens, Stick, Lucky Punch (Stick and Scope Lens were missing from the items).
- 53c8f2a White Herb (missing from the items; ~3% of randbat sets).
- 6eb9cfd Hustle does not lower the accuracy of moves that never miss (Aerial Ace); gen3 and genx, new
  `Choices::never_misses`.
- 4a0c975 Sleep Talk's called move branches on damage (crits, KO rolls) like any other move; gen3 and genx.
- a1b1314 Sturdy does not survive lethal hits at full HP (that is Gen 5+; Gen 3 Sturdy only blocks OHKO moves).

## Left (7 of 991)
- KO timing (4): Showdown runs end-of-turn effects after the mid-turn replacement, the engine before it. Even the
  KO-aware score cannot hide a Pokémon that the engine's end-of-turn poison knocks out on the same turn. Matching it
  means moving end-of-turn after the replacement on KO turns, a structural engine change. Small effect in play: the
  replacement misses that turn's Leftovers/poison.
- Pinch berries and averaged rolls (2): the engine averages damage rolls unless a roll can KO, so a Salac/Petaya
  threshold inside the roll range is either always or never crossed. Would need branching on berry thresholds.
- Ditto Transform (1): the test's summary named a transformed Ditto by its new species; `engine_diff.js` is fixed for
  future runs.
- Not checked: Intimidate order when both sides switch looked like a bug but was White Herb. OHKO moves and Sturdy's
  OHKO immunity are not implemented in Gen 3. Skipped by the converter (not counted): Truant, Perish Song, two-turn
  moves, trapping, Yawn, Destiny Bond.

## Files
- `engine_diff.js`: the test (Gen 3 port: format, Gen 3 sleep counter hook and redraw, mid-turn flag, saves the full
  Showdown distribution). Converter: `ocean/pokemon9/showdown_convert.js` (now: Hidden Power power from Showdown, Gen 3-4
  Wish amount).
- `run.sh`: `ENGINE=<copy of diffcheck> run.sh 4 250 200`. Use a copy: `cargo test` rebuilds target/release/diffcheck.
- `rescore.js`: scores saved Showdown samples against another engine build in seconds:
  `gunzip -k run.jsonl.gz; [KO_SPLIT=1] ENGINE=.../diffcheck node rescore.js run.jsonl out.jsonl; python3 diff_report.py out.jsonl`.
- `run.jsonl.gz`: the 1,000 positions with Showdown's full outcome distributions. `report_{base,fixes7,final}.txt`: both
  scores for the three engine builds in the table.
- Engine build: `cargo build --release --no-default-features --features gen3 --bin diffcheck` in
  `forks/poke-engine-gen3` (worktree of the fork, branch gen3-diff).

## Next
Upstream PRs if wanted (each commit stands alone), then a Gen 3 env like ocean/pokemon9 (poke-engine with
--features gen3) for Gen 3 random battles and OU.
