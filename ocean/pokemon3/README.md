# Pokémon Gen 3 random battles (ocean/pokemon3)

Singles Gen 3 random battles for PufferLib, on [poke-engine](https://github.com/pmariglia/poke-engine) (Rust, built
with `--features gen3`) with the fixes in our fork, [FelixAllistar/poke-engine](https://github.com/FelixAllistar/poke-engine)
(branch `gen3-diff`, one commit per fix with a regression test). A port of `ocean/pokemon9` (Gen 9).

## Accuracy

A differential test against Pokémon Showdown plays the same 1,000 Gen 3 random-battle positions in both and compares
the outcome distributions (`reports/pokemon_experiments/20261105_gen3_engine`). The fork as it was agreed on 905/991;
with 14 Gen 3 fixes, 984/991 (99.3%). Known gaps: on a turn with a KO, Showdown's Gen 3 sends the replacement in
before end-of-turn effects and the engine after; pinch berries see average damage rolls.

## Build and train (Linux, CUDA)

```sh
# Rust (cargo) and Node with pokemon-showdown (build/pokemon/showdown_server/node_modules) are needed once.
NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/build_positions.js 100000 build/pokemon3/positions.txt 0
./build.sh pokemon3            # runs build_engine.sh: clones the fork at a pinned commit, builds the static library
./puffer train --base.env_name=pokemon3
./puffer eval latest --base.env_name=pokemon3 --headless --env.bot_policy=1   # win rate against a random-move opponent
```

`build_positions.js` makes turn-1 positions with Showdown's own Gen 3 random-battle team generator (20K take about
2 minutes on one core; shard bigger runs with the last argument). It uses the differential test's converter,
`ocean/pokemon9/showdown_convert.js`. `gen_vocab.py` regenerates `engine/src/vocab.rs` (the ability and item slots of
the observation) from a positions file: 71 abilities and 13 items in Gen 3 random battles, all defined in the engine.

## Environment

- Both players are agents (simultaneous moves), so mirror self-play trains both seats. Reward +1 win, -1 loss, 0 for a
  draw at the 500-turn cap. Battles that hit an engine panic end as a draw and are counted in `engine_errors`.
- 10 actions: four moves, six switches. Illegal ones are masked. No Terastallization, terrain or Trick Room in Gen 3.
- One outcome per turn is sampled from the engine's branches, with damage branching on (critical hits and knock-out
  rolls are real outcomes; damage within a roll is averaged).
- Observation (5,171 values), partial information like a player: your whole team; the opponent's Pokémon once seen,
  their moves once used, item once a player would see it (Leftovers at once, any item once it changes), ability once
  Showdown's Gen 3 announces it on switch-in (Intimidate, Drought, Drizzle, Sand Stream, Trace). Per Pokémon: HP,
  stats, types, status, one-hot ability and item, and per move its type, category (by type, as in Gen 3), power,
  accuracy, priority, PP and effects (boosts, status chances, drain, recoil, flags).

## Showdown bridge (ocean/pokemon3/showdown)

Plays a pokemon3 checkpoint on a Pokémon Showdown server (local; Metamon evals, gen3ou or gen3randombattle).
- `tracker.js` follows a battle from our seat (protocol and requests) and rebuilds the poke-engine state with what a
  player knows: our side exactly; the opponent's species, level, HP %, status, boosts and conditions, revealed moves,
  items and abilities; unseen stats estimated as random-battle sets (85 EVs, 31 IVs, neutral nature; 3% off on
  average). Party order is fixed as in training.
- `policy_server.c` turns STATE + SEEN into the env's own observation (`pk3_view` in engine/src/lib.rs) and runs the
  network on the CPU (src/puffercpu.c). Argmax by default, `--sample` to sample.
- `client.js` connects, challenges/accepts/ladders, and logs results; `--team-dir` draws a team file per battle.
- Checks: `check_view.c` plays env battles through the bridge path and asserts the observation equals the env's (all
  20,589 turns equal; win rate vs random matches the GPU eval: argmax 80.3% vs 80.5%, sampled 94.7% vs 92.0%).
  `check_tracker.js` compares the tracker's state with Showdown's full state over 4,783 decisions: species, HP,
  stats, moves and PP match; the remaining differences (Substitute HP after hits, a Toxic counter off by one, the
  Protect counter scale) are each under 3% of decisions.
- On a local server (`reports/pokemon_experiments/20261106_gen3_bridge`), the 65M checkpoint beat a random-move bot
  27-3, as in the env (92%).

Build: `bash ocean/pokemon3/showdown/build.sh` (after build_engine.sh). Run:
`NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/showdown/client.js --server ws://localhost:8000/showdown/websocket --name NAME --checkpoint CK.bin --challenge USER --format gen3ou --team-dir DIR`.

## Results so far

2026-11-06, local smoke test (GTX 1060, 3 cores): 256x2 policy, 3M steps of self-play in 47 seconds (54K steps/s):
81.8% wins against a random-move opponent over 10,179 games, no engine errors.
