# Showdown bridge (in progress, started 2026-10-27)

Goal: our policy plays real Pokémon Showdown Gen 1 OU battles, starting with a local server, then the public ladder.
The policy reads a 648-byte observation (`pk_observe` in `../bridge.zig`); the bridge rebuilds those bytes from what
Showdown sends one player (protocol messages plus its request JSON) and maps the policy's action back to a choice.

## Step 1, done: our engine reproduces Showdown exactly

`check_engine.py` plays random-choice battles in Showdown and replays the same choices in our engine from the same seed
(the engine derives Showdown's four-part seed via `PSRNG(seed).newSeed()`). After every decision the turn number and every
Pokémon's HP must match. Result: **100 battles, 10,783 decisions, 0 divergences** (Metamon's 28 competitive teams).

Needed for an exact match (all in `battle.ts`):
- `@pkmn/sim` 0.9.31, the Showdown build pkmn/engine is validated against, with pkmn's own test patch
  (`src/test/showdown.ts`: deterministic ordering of same-speed event handlers). Real Showdown breaks some speed
  ties with extra random draws, so live battles won't replay draw-for-draw; the bridge doesn't need them to.
- Format `gen1customgame@@@Endless Battle Clause,Sleep Clause Mod,Freeze Clause Mod` (Gen 1 OU's clauses).
- Every set: `No Ability`, no item, level 100, EVs 255 (maximum stat experience), IVs 31 (DV 15).
  Without `No Ability`, Custom Game silently gives modern default abilities (e.g. Intimidate on Tauros).

```sh
ocean/pokemon/showdown/build.sh                 # bundles battle.ts, builds engine_replay
python3 ocean/pokemon/showdown/check_engine.py 100
```

## Step 2, done: tracker (Showdown messages -> observation)

`tracker.ts` rebuilds the 648 bytes from one player's protocol lines and request. `check_tracker.py` compares it with
`pk_observe` on the identical battle before every decision: **99.70% of observations byte-identical** (60 battles,
13,058 observations). Remaining differences are rare hidden-state edges (Binding's end, a recharge case, a stat quirk).
Gen 1 details it needed: Special boosts are announced twice (spa, spd); a fainted Pokémon loses its status and volatiles;
the stat modification glitch (a boost re-applies the opponent's paralysis/burn drop, a drop re-applies the target's own);
a forced move (recharge, locked) is slot 0 ("move 1"), Struggle is action 11.

## Step 3, done: policy server

`policy_server.c` loads a checkpoint and answers observations per battle (own recurrent state each). `check_policy.py`:
the 2B run's 1875M checkpoint picks **the same action through the bridge as on our engine 99.97%** of the time (6,598
decisions), **99.67% with ladder-style percentage foe HP** (`PERCENT_HP=1`).

## Step 4, working: live client

`client.ts` connects to a Showdown server (websocket), logs in (local: any name; public: `PS_PASSWORD` in the environment),
challenges, accepts or searches the ladder, and plays with the policy server. Self-play on a local Showdown 0.11.11
server (`build/pokemon/showdown_server`, started with `--no-security`) completed full Gen 1 OU battles without errors.

```sh
node build/pokemon/showdown_parity/client.js --name NAME --team TEAM --checkpoint CK.bin --accept --battles 10
node build/pokemon/showdown_parity/client.js --name OTHER --team TEAM --checkpoint CK.bin --challenge NAME --battles 10
```

## Step 5, done: vs Metamon on real Showdown (box, 2026-10-27)

Local Showdown 0.11.11 server on the box (tmux `showdown`); Metamon runs through its own client in challenge mode
(`reports/pokemon_experiments/20261027_showdown_vs_metamon/metamon_challenge.py`: Metamon unchanged plus our evaluator's
two shims, the torch attention backend and an integer traj_save_len), playing its 28 competitive teams; our client brings
zam_egg_cloy_don with the 1875M checkpoint's drafted sets (`our_teams_2b1875_sets`, export_drafted_teams.py).

| Opponent | Showdown (100 battles, +/- 9) | Native full game (448) |
|---|---|---|
| Kakuna | 68-32 | 69.0 |
| TaurosV0 | 70-30 | 73.0 |

Metamon's own tallies agree (win rate 0.32 / 0.29; 99.8% valid actions); no errors on either side.

## Next
6. Ladder: the user creates the account and sets PS_PASSWORD; we run the client with --ladder.
