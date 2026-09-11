# Opening spending audit and fresh-run handoff

Remote results: `logs/kaggriculture/opening_spending_v3/comparison.tsv` and
per-policy episode JSON. Stochastic CPU/NumPy diagnostics, 14 games/policy,
same opponents and seeds as previous diagnostic runs. Reference is the user's
`106946670.json` (two players, one different seed/match); it is descriptive,
not a controlled causal comparison or a GPU score benchmark.

## Actual counting bug

`kg_set_tile_bits` sets `animal_bits` for **all coops/pastures**, including
empty ones (`animal == KG_ANIMAL_INVALID`). `kag_live_tiles(...,1)` counts this
bitset. Thus the old `animals` diagnostic field includes empty housing.
`cows` explicitly checks the animal ID and remains valid.

The phase helper also treats this bitset as occupancy: invalid animal IDs
count as `other_animals` and as productive plot occupancy. A constructed
state with 12 EMPTY pastures at turn 96 reports 12 other animals, 1 productive
plot, and phase reward 0.0104167 at scale 2. This is not intended behavior.
The trainer/helper have NOT been changed by this audit. Disable phases for
fresh training until valid-species occupancy checks and regression tests are
implemented and the trainer rebuilt. CPU/GPU parity alone cannot catch a
shared semantic error. Expansion peak animal counts may be affected too.

The new diagnostic guards invalid IDs and exposes occupied animals and empty
housing separately. The first spending-probe v1 attempted species indexing
without that guard and failed with memory corruption; do not use v1 outputs.
V3 includes the guard, unplaced livestock, seed holdings, and accounting checks.
V2 is the earlier guarded diagnostic without private inventory counts.

## Findings (final phase policy, diagnostic means)

At turn 24: 1.43 placed cows, 1.57 sheep, 7.5 EMPTY animal buildings,
0.86 unplaced cows and 1.14 unplaced sheep (shed + carried), 5.5 wheat plants,
17.36 unused wheat seeds, no melons. Cash $119; purchases $2,874; hires $7.
The reference players already have 2 cows + 2 sheep, 7–12 melons, and 7–8
other crops planted. Our livestock is purchased ahead of placement, and our
early farm allocates far less to long-maturing crops.

At turn 192: our final phase policy still averages 1 plot, 6.29 cows,
5 sheep, 6 melons, and ~0.07 strawberries. Reference: 2 plots, 7–8 cows,
2 sheep, 10–12 melons, and 16–18 strawberries. This is a production-timing
gap, not merely failure to liquidate at the end. It does not establish whether
policy choices, executor scheduling, or both cause the gap.

Hiring is NOT the main early cash drain: cumulative cost ~$56 by turn 192.
Derived exactly for these root episodes as starting cash + cumulative sales
- cumulative market purchases - cash - cumulative land costs (0/1000/3000/7000).
Reference cashflow categories are not inferred from submitted orders, which
could fail; only public farm state is compared.

## PSRO and league

Analysis prefix: `logs/kaggriculture/psro_fuzzy_phases_v2_diversity`.
298.84M phase checkpoint leads the ten-policy 20-game screen (mean score .85)
and the three-candidate 100-game confirmation (mean score .565). Confirmation
contains only the three phase checkpoints, not a fresh 100-game rerun against
every old league member. No global exploitability claim follows.

Added `run_fuzzy_phases_v2_t1_a0_0000000298844160` as one new opponent at 15%
of league sampling mass. Preserved all seven old members and their relative
weights (scaled to 85% combined). This is a deliberate diversity allocation,
NOT the solved equilibrium weights. No pruning, submission, or learner change.
Backup: `saved/kaggriculture_league_obs1_expansion_v1_archive/diversity_add_20260909_064515_493732`.

## Fresh model

Remote active `config/kaggriculture.ini`: `load_model_path=None`, unique
`run_id=fresh_phases_20260909_v1`, explicit `reward_phase_scale=0` because of
the bug above. All other existing settings preserved: LR .0009, no annealing,
4096 agents, horizon 256, minibatch 2048, 256x3, EMAG .01/tau .1, root starts,
obs1/macro2. Existing reward settings are NOT the frozen ablation's settings:
legacy potential 1, progress scale 1 (component multipliers zero), terminal
cash 1, terminal win 1. No fixed clone magnet. Training not launched.
Config backup: `logs/kaggriculture/fresh_setup_20260909/config_before.ini`.
