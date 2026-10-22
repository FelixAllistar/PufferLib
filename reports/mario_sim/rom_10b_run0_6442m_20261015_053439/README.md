# Mario sweep run 0: 6.44B-step ROM evaluation

The same freshly trained 10B trial, evaluated at 2,147,483,648 and 6,442,450,944 steps. Both evaluations use the same evaluator, inputs, settings, sampled FP32 CPU inference, and seed 137.

| Test | 2.15B checkpoint | 6.44B checkpoint |
| --- | --- | --- |
| FPG, depth 96 | 62/64 | 64/64 |
| FPG, depth 425 | 62/64 | 61/64 |
| Fresh 1-1 clears | 29/32 | 25/32 |
| All-stage mean game progress | 4124.375 pixels | 8262.59375 pixels |
| All-stage clear-reward events | 33 | 77 |
| All-stage full-game wins | 0 | 0 |
| All-stage timeouts | 2 | 0 |

Validation: 192/192 episodes completed; 0 simulator/ROM mismatches. All panels validated: True.

fpg_96: comparing the 62 seeds successful in both checkpoints, mean duration changed from 147.97 to 132.00 frames (+10.8% reduction).

fpg_425: comparing the 59 seeds successful in both checkpoints, mean duration changed from 306.42 to 284.46 frames (+7.2% reduction).

new_game_1_1: comparing the 22 seeds successful in both checkpoints, mean duration changed from 2112.55 to 2133.55 frames (-1.0% reduction).

Mean game progress changed by +100.3%. Old sweep run 42 achieved 6,696.969 pixels and 62 level clears on this panel; run 12 achieved 3,486.375 pixels and 22 clears.

These fixed-seed ROM tests do not determine the final native sweep score. Source training continues without intervention. Full metadata, commands, and per-panel outcomes are in summary.json.

Metric clarification: the native continuing-game `level_clears` field counts clear-reward events (flag contact, castle victory, or stage advance), deduplicated by level within each episode. It can count a flag contact without the finish sequence completing. These are not unique levels or per-level completion rates. Per-level retests require a completed level exit.

[Paired per-level retest and consolidated game/FPG report](/home/felix/puffertank/pufferlib/reports/mario_sim/rom_levels_2b_6b_20261015_060222/results/README.md)
