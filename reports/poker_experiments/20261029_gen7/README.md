# Poker gen6 / gen7 (2026-10-29, box 57608917)

- gen6: reward_scale 0.01, 3B steps from scratch (gen4 recipe otherwise). Offline panel worst +1.38 BB/hand, web field
  +0.92; six copies of itself: re-raise wars (a street with 3+ raises) 54.9% of hands, 2+ players all in 36.7%
  (gen5: 82.7% / 71.7%). leagues/poker/gen6/gen6.bin.
- gen7 (gen7_selfplay.sh): from gen6, 95% of non-panel seats are copies of itself (rules_fraction 0.05), 1B steps.
  A, newest snapshot only (sp_newest): training panel_worst 2.03; vs bots slightly better than gen6; six copies: wars
  41.8%, 2+ all in 23.1%. On the web game as "Puffer gen7" (29 Oct). B, the four most recent snapshots (sp_recent4):
  running. results_partial.txt.
