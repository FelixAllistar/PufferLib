# Kaggriculture top-replay opening (2026-08-08, module 1.32.6)

Mined from the top ~10 scored episodes in the kaggle/kaggriculture-episodes-2026-08-08
dataset. Rewards ~140k-146k. Both players use an identical opening.

## Canonical opening (player 0 and 1, first 10 turns)

- t0: farmer PASS, market []
- t1: farmer PASS, market = HIRE x5, BUY_ANIMAL COW 2, BUY_ANIMAL SHEEP 2,
       BUY_SEED WHEAT 7, BUY_SEED MELON 10, BUY_PRODUCT WHEAT 5
- t2: farmer PICKUP SHEEP 1, market SELL WHEAT 1 (sometimes x2)
- t3: farmer PICKUP WHEAT 1, market BUY_PRODUCT WHEAT 1
- t4: farmer WEST, market SELL WHEAT 1
- t5: farmer WEST, market BUY_PRODUCT WHEAT 1
- t6: farmer BUILD_PASTURE
- t7: farmer PLACE SHEEP
- t8: farmer FEED WHEAT
- t9: farmer CARE
- t10-t25: farmer PASS (waiting for sheep/cow to mature)
- t26+: animal care loop: PICKUP WHEAT -> move -> FEED -> CARE ->
       COLLECT_FERTILIZER -> move -> repeat (roughly deterministic)

## Consistency

All top-10 sampled episodes are identical through t9 except tiny market-order
count differences at t2/t4 (SELL WHEAT 1 vs SELL WHEAT 1, SELL WHEAT 1).
The t10-t44 action-count histogram is identical across all five deep samples:
19 PASS, 1 PICKUP, 2 NORTH, 1 FEED, 4 CARE, 4 COLLECT_FERTILIZER,
2 WEST, 2 SOUTH. The dominant late strategy is a small animal herd (sheep +
cow) with a feed/care/fertilizer loop, not broad crop diversification.

## Optional native experiments

`env.opening_turns` (default 0) forces learner-policy seats through this sequence
for the first N turns. The mask leaves exactly one legal action per active
head, so the policy gets zero gradient on forced heads while PPO trains the
value function and the post-opening game. Suggested values:
- 10 = shared opening only
- 26 = opening + the PASS wait period
- 40+ = also attempts the care loop, but that depends on animal position, so
  it is riskier and may mask to PASS when a forced move is illegal.

The submission Python adapter would need the same mask if deployed; currently
it is not wired there.

`env.reset_opening_turns` (default 0) is a separate reset-state experiment.
At every episode reset it samples `k` uniformly from `[0, N]`, executes `k`
real simultaneous turns of the canonical opening for both farms, and presents
that resulting state as PPO's initial observation. No rewards or gradients are
fabricated for the skipped prefix: the policy simply wakes up at the handoff.
Keeping `k=0` in the mixture preserves ordinary turn-zero starts.

Both knobs are independent and default off. The old numbered curriculum and
pre-unlocked-land drills were removed: they restricted legal actions and
changed the game state in ways that taught policies which did not transfer to
the full Kaggle ruleset.
