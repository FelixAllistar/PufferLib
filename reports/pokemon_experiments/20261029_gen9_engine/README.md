# Gen 9 engine: poke-engine vs Pokémon Showdown (2026-10-29, Pokémon box 57608917)

Goal (user): a fast Gen 9 engine (random battles and OU, the full game), then every generation, for videos. Option 1:
make pmariglia/poke-engine (Rust, Gen 1-9 singles, built for search) one-to-one with Showdown; option 3: Showdown in
the loop, sped up. Engine changes go upstream as atomic commits with stated trade-offs (SKILL_ISSUES.md style).

## Speed (one core, random-move Gen 9 random battles)
- Showdown in-process (pokemon-showdown Battle, no server or streams; showdown_bench.js): 1,018 turns/s, 17 games/s.
- poke-engine, one sampled outcome per turn (selfplay_bench.rs): 583,231 turns/s, 14,307 games/s. About 570x faster.
- poke-engine's own MCTS benchmark: 2.02M iterations over its 101 gen9 random-battle states in 5.9 s.

## Accuracy harness
- diffcheck.rs (built into poke-engine as a bin): STATE, MOVE1, MOVE2 -> every outcome with its probability (damage rolls
  branched), summarised as actives, each Pokémon's HP and status, the active's boosts.
- engine_diff.js (in build/pokemon/showdown_server on the box): plays random gen9randombattle games in Showdown, snapshots
  positions at random turns, converts them to poke-engine state strings (full information), picks a random legal move
  pair, samples Showdown's outcome N times from a cloned battle (sim State.serializeBattle, fresh PRNG seeds), and
  compares: max gap in expected HP per Pokémon (% of max HP), TVD of actives+statuses, TVD of boosts. Positions using
  things the converter does not map yet are skipped and counted.
- diff_report.py: agreement rate and the worst positions.

## First run (try3.jsonl, 40 positions x 400 Showdown samples)
26/40 agree (HP within 3%, TVDs <= 0.08). The 14 mismatches are not yet classified into converter gaps (e.g. switch-in
abilities, Slow Start, Illusion not mapped) vs engine differences; that is the next step.
