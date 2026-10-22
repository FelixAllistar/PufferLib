# Mario 10B sweep run 0: interim ROM evaluation

Checkpoint: 2,147,483,648 steps, 21.5% of the requested 10B budget.

| Test | This checkpoint | Earlier result |
| --- | --- | --- |
| FPG, reference depth 96 | 62/64 successes | Run 26: 63/64 |
| FPG, reference depth 425 | 62/64 successes | Run 26: 64/64; run 42: 51/64 |
| Fresh 1-1, first level clear | 29/32 | Run 12: 13/32 |
| Game, all 32 stage starts, mean progress | 4,124.375 pixels | Run 12: 3,486.375; run 42: 6,696.969 |
| Game, all 32 stage starts, clear-reward events | 33 | Run 12: 22; run 42: 62 |
| Full-game wins | 0 | Run 12: 0; run 42: 0 |

All 192 episodes completed with zero native/ROM mismatches. Sampled FP32 CPU inference, seed 137, same evaluation settings as earlier panels. Downloaded weights and curriculum sidecars verified with SHA-256.

The all-stage game panel recorded 30 deaths and two timeouts. Both long episodes stalled in the flag sequence on 7-1. Run 26 has no valid completed all-stage comparison due to its earlier parity failure.

FPG is close to run 26's performance. The 1-1 result is stronger than run 12's, while broad game progress is about 38.4% below run 42's. These fixed-seed panels do not establish the final 10B sweep score.

Complete metadata, commands, baselines, and results: `summary.json`. Episode records and trajectory CSVs are in each panel directory. Remote training was not modified.

Metric clarification: the native continuing-game `level_clears` field counts clear-reward events (flag contact, castle victory, or stage advance), deduplicated by level within each episode. It can count a flag contact without the finish sequence completing. These are not unique levels or per-level completion rates. Per-level retests require a completed level exit.

[Paired per-level retest and consolidated game/FPG report](/home/felix/puffertank/pufferlib/reports/mario_sim/rom_levels_2b_6b_20261015_060222/results/README.md)
