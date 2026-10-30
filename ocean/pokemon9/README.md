# Pokémon Gen 9 random battles (ocean/pokemon9)

Singles Gen 9 random battles for PufferLib, on [poke-engine](https://github.com/pmariglia/poke-engine) (Rust) with the
fixes in our fork, [FelixAllistar/poke-engine](https://github.com/FelixAllistar/poke-engine) (branch `gen9-merged`;
each fix is its own `fix/*` branch with a regression test).

## Accuracy

A differential test against Pokémon Showdown plays the same 1,000 positions in both and compares the outcome
distributions (`reports/pokemon_experiments/20261029_gen9_engine`). Upstream poke-engine agreed on 890/1,000; with the
fork's fixes and the test's own converter fixes, 997/1,000. Known gaps: a contact status (Flame Body) landing in the
middle of a multi-hit move, and Leppa Berry.

## Build and train (Linux, CUDA box)

```sh
# Rust (cargo) and Node with pokemon-showdown (npm i pokemon-showdown) are needed once.
NODE_PATH=path/to/node_modules node ocean/pokemon9/build_positions.js 100000 build/pokemon9/positions.txt 1
./build.sh pokemon9            # runs build_engine.sh: clones the fork at a pinned commit, builds the static library
./puffer train --base.env_name=pokemon9
./puffer eval latest --headless --env.bot_policy=1   # win rate against a random-move opponent
```

`build_positions.js` makes turn-1 positions with Showdown's own Gen 9 random-battle team generator (100K is plenty;
shard the run with the last argument). `gen_vocab.py` regenerates `engine/src/vocab.rs` (the ability and item slots of
the observation) from a positions file.

## Environment

- Both players are agents (simultaneous moves), so mirror self-play trains both seats. Reward +1 win, -1 loss, 0 for a
  draw at the 500-turn cap. Battles that hit an engine panic end as a draw and are counted in `engine_errors`.
- 14 actions: four moves, the four moves with Terastallization, six switches. Illegal ones are masked.
- One outcome per turn is sampled from the engine's branches, with damage branching on (critical hits and knock-out
  rolls are real outcomes; damage within a roll is averaged).
- Observation (7,584 values, bf16 in bf16 training), partial information like a player: your whole team; the
  opponent's Pokémon once seen, their moves once used, Tera type once used, item once a player would see it, ability
  once announced on switch-in. Per Pokémon: HP, stats, types, Tera, status, one-hot ability and item, and per move its
  type, category, power, accuracy, priority, PP and effects (boosts, status chances, drain, recoil, flags).

## Results so far

v1 observation (no abilities/items), 512x2 policy, 100M steps of self-play in 5 minutes: 92.6% wins against a
random-move opponent. The v2 observation (this one) has not been trained yet.
