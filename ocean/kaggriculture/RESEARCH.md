# Kaggriculture research log

## 2026-08-05: PSRO learner selection and behavior profiling

PSRO now treats the highest confirmed meta weight among the candidate run's
checkpoints as the next learner, rather than assuming the final checkpoint is
best. It records the candidate run ID, source checkpoint, active league copy,
and confirmed weight in `saved/kaggriculture_league_v3/learner.tsv`, updates
`config/kaggriculture.ini`, and writes a native bench-only behavior profile
beside the PSRO logs. `--reuse` infers the run directory from the reused
manifest, so an unrelated newest run cannot be paired with an old screen.

The first semantic profile of the active v3 league was intentionally tiny (one
game per seat) but already exposed the important split: the recent meta
policies issued land orders and crop/market actions, but had zero animal buys,
placements, feed, care, or animal harvest in the probe. Several old policies
occasionally bought or placed an animal but also produced no feed or animal
harvest. This means current payoff/behavior JSD is not evidence of meaningful
animal-policy diversity. The 1785902495829 100M learner probe was weaker than
the confirmed 1785898836191@52.43M member and repeatedly sold fertilizer; it
should not be promoted merely because it is the newest checkpoint.

The native policy ABI still has one market head. The rule core accepts the
official ten-order queue, but the learner emits one market command per turn.
Python Kaggle agents containing a large `AGENT_SOURCE = """..."""` value are
frozen source/trace agents, not native neural weights; they can be ported to a
C scripted evaluator or run only in an offline Python comparison, but cannot
enter the C/CUDA frozen bank without a native port.

## 2026-08-03: native selfplay and curriculum

All training and simulation results below use the native C simulator and the
C/CUDA policy path. Python is not part of the simulator or training hot path.
Evaluation numbers are panel-dependent; money from the live dashboard, broad
cross-play, and a strongest-opponent confirmation are not interchangeable.

### Run ledger

| Run | Effective setup | Result | Interpretation |
|---|---|---|---|
| `1785676479914` | Hard PFSP; 93.4% base weight on reachable `1785670659248@216.27M`; `gamma=0.999899983`; `gae_lambda=0.98`; LR `0.0004` | Produced the current champion `@658.64M`, roughly $13k-$15k depending on panel | Stable, narrow curriculum can bootstrap a strong economy |
| `1785685929832` | Same 32x2 model but LR `0.04` | Sampled checkpoints stayed near the $500 reserve | Invalid optimization experiment: LR was 100x too high |
| `1785714053666` | Uniform 19-policy pool with hard PFSP; `gamma=0.99` | Broad peak near $3.6k, then regressed below $1k | Invalid pool comparison: `0.99^720` nearly removes terminal credit |
| `1785720115458` | Uniform 19-policy pool with hard PFSP; restored gamma | Best sampled checkpoint `@353.89M` confirmed near $817; final near $544 | Hard PFSP over-prioritized losses and converged toward low-money exploit cycles |
| `1785727717634` | Variance PFSP; 10% uniform exploration; 50/50 external/history; 1M swaps | `@399.77M` broad score `0.807`, broad money $8.0k, direct 500-game confirmation $8.35k; `@697.96M` broad money $8.62k but only $3.82k in the strongest four-policy confirmation | Curriculum failure fixed. Different checkpoints optimize robustness versus weak-panel economy; final checkpoint is not automatically best |

### Lessons that should remain invariants

1. **PSRO weights are not training probabilities.** The solved `70/25/5`
   role mixture belongs in `saved/kaggriculture_league/manifest.tsv` for
   metagame accounting. Copying a degenerate solver distribution into hard
   PFSP can starve the learning curriculum.
2. **Use near-undiscounted credit for a 720-turn economy.** At `gamma=0.99`, a
   terminal signal has weight about `0.0007` at the beginning of the season.
   At `gamma=0.999899983`, it retains about `0.93`. The current potential-delta
   shaping also aligns with final cash only when discounting is near one.
3. **Hard PFSP is wrong for a fresh sparse-reward response here.** Prioritizing
   `1-score` repeatedly selects opponents holding the learner to 2-10% and
   dilutes useful gradients across incompatible market regimes.
4. **Variance PFSP is materially better.** Priority `4*s*(1-s)` emphasizes
   competitive opponents. A true 10% uniform branch preserves exploration.
   The first full run sampled all 19 external policies and 164 history policies,
   and produced the first new confirmed strong response.
5. **Keep intermediate checkpoints.** Non-transitive behavior and economic
   quality oscillate. In the variance run, `@399.77M` was the robust response,
   `@697.96M` had the highest broad-panel money, and the final policy was worse.
6. **Dashboard money is a training-mixture diagnostic, not a population rank.**
   The variance run ended around $1.36k on the dashboard while its final
   checkpoint averaged about $5.1k over broad cross-play.
7. **Analysis must not mutate the league.** Use `psro.sh analyze` while
   diagnosing. Admit only after seat-balanced confirmation.

### Curriculum terminology and bootstrap history

Kaggriculture does **not** currently have a task/environment curriculum in the
Bomberman sense. Every recorded training run used the full 720-turn game with
the complete farm, market, season, and action set. There were no staged short
harvest puzzles, simplified maps, restricted mechanics, privileged starting
states, or automatic progression from easy tasks to the full game.

Three separate mechanisms have sometimes been called “curriculum” in earlier
discussion and should not be conflated:

1. **Opponent curriculum:** native starter/crop/economy bots, learned league
   checkpoints, and PFSP alter which opponent a learner faces while leaving the
   game itself unchanged.
2. **Reward shaping:** each step receives the change in the learner's economic
   potential rather than waiting only for the turn-720 outcome. Potential is
   cash plus marked seeds, products, planted crops and held yield, animals, and
   purchased land. Assets are written down near season end and terminal
   win/loss is added separately. This improves temporal credit assignment; it
   is not a curriculum because the task does not change over training.
3. **Population bootstrapping/warm starts:** useful checkpoints become future
   opponents or initial weights. This creates a sequence of increasingly useful
   responses without simplifying the environment.

The first useful population was bootstrapped as follows. Short cash-ranked
sweeps against native opponents produced the weak/medium v1 league. Run
`1785662154894` trained against that population and supplied useful 72M, 144M,
167M, and 216M stages for v2. PSRO then promoted
`1785665233058@478.41M`, followed by `1785670659248@216.27M`. Champion run
`1785676479914` loaded no learner checkpoint, but it was not learning in an
unstructured vacuum: 75% of games used the mixed economy bot and the remaining
learned-opponent lane had 93.4% base weight on the reachable 216.27M policy.
Near-undiscounted returns, persistent entropy, and dense economic potential
shaping completed that bootstrap. Thus the champion began from random weights,
but its opponents were heavily staged.

The checked-in default still has an opponent curriculum: 75% mixed economy bot
and 25% learned PFSP-bank games. With `bot_rules_fraction=1.0`, the easier crop
specialists are no longer selected. This is a broader and harder opponent mix
than the narrow bootstrap that produced the original champion.

A genuine task curriculum remains an unimplemented option. A sensible sequence
would be: (1) short buy/plant/water/harvest/sell loops for wheat and carrot;
(2) one-quadrant farming with hiring and multiple crops; (3) states near
first-quadrant saturation with enough cash for BUY_LAND; (4) the complete
30-day market game; and finally (5) full-game-only self-play. Any temporary
`reward_land_value > 1` experiment is only a cheap land-discovery shaping
intervention, not that task curriculum. It should be annealed back to `1.0`
and evaluated by land purchases, terminal cash, and cross-play performance.

### Policy retention

Do not retain a percentage of checkpoints. Keep explicit roles:

- a hall of fame containing at least the top three seat-balanced broad
  cross-play policies;
- the best confirmed economy policy when it differs from the robust champion;
- a small JSD-selected set of exploiters that closes distinct payoff gaps;
- archived sources for every admitted policy so pruning is recoverable.

The current champion is still `1785676479914@658.64M`. The new
`1785727717634@399.77M` policy is worth preserving as a strong distinct response,
not as a replacement for the champion. `@697.96M` is an economy candidate but
is substantially weaker against the strongest panel. Independent copies and
their roles live in `saved/kaggriculture_hall_of_fame`; PSRO never prunes that
directory.

### Sweep protocol

The original 20M-step sweep was not reliable for selfplay. Useful behavior now
appears after roughly 200-400M steps, and with `selfplay.eval_games=0` each trial
was ranked by one noisy final rollout against whichever opponent was active.

The native sweep now supports a fixed-panel objective. Kaggriculture evaluates
both seats against the same first eight external policies, 50 games per seat,
and ranks by mean final money. Use common initialization and common random
numbers for the search:

```bash
./ocean/kaggriculture/sweep_selfplay.sh \
  saved/kaggriculture_hall_of_fame/variance_robust_400m.bin \
  100000000 48
```

This is a continuation sweep, not final evidence. The original `sweep_only`
silently restricted the search to four curriculum parameters even though the
file defined optimizer and reward spaces. After the first Kaggle replay audit,
the broad search includes 22
high-impact parameters: opponent sampling and payoff tracking, PPO credit and
update dynamics, EMAg, simultaneous frozen-opponent diversity, the
mark-to-market scale/liquidation schedule, and native-bot mix. Asset conversion
weights are fixed at `1.0`; sweeping them below parity directly penalized seed,
animal, and land purchases.
Policy shape and replay ratio remain fixed so every trial is checkpoint
compatible and avoids the stale-rollout KL failure already observed. With 22
dimensions, use at least 48 trials; use fewer only for a deliberately narrowed
CLI override. Confirm the top three suggestions from fresh initialization with
multiple seeds through at least 400M steps, then run the normal PSRO cross-play
analysis. Do not mutate the active league during a sweep.

The first sweep smoke exposed a separate evaluation bug: generic model-match
mode left Kaggriculture's `bot_opponent_fraction=0.75` active, so one requested
model seat was silently replaced by the scripted economy bot. That produced an
impossible $27.6k score for an untrained 2M-step policy. Match mode now forces
both scripted-bot fractions to zero. The corrected smoke returned $500.25.

### External validation

Submission `55202900` packages only our `champion_658m.bin` plus a standalone
NumPy implementation of the native 488-byte observation, five masked action
heads, and two-layer MinGRU. Kaggle validation completed at public score 600.
The first archive failed because Kaggle executes from `/kaggle/working` while
extracting assets under `/kaggle_simulations/agent`; resolve checkpoint assets
relative to the executing code filename, not the process working directory.

The first five public replays exposed a training pathology hidden by broad
money averages. The champion bought no land in any episode. Three episodes
collapsed to the $500 policy reserve. In episode 89661029 it peaked at $22,318,
bought 624 fertilizer, sold 460, and finished holding 164 fertilizer at $3,198.
In screenshot episode 89657588 it peaked at $20,143; the replay frame showed
$9,238 before its final four-fertilizer buy, and official terminal cash was
$8,762. The written rules say fertilizer cannot be sold, but the actual Kaggle
engine accepts `SELL FERTILIZER`; native behavior is therefore correct on that
point.

The cause was reward timing rather than inference parity. Seeds were marked at
`0.2x` purchase cost and land/animals at `0.8x`, directly punishing productive
conversion, while products were marked at `1.0x` until an abrupt terminal
write-off. All asset conversion weights are now fixed at `1.0`. Non-cash asset
potential stays full through day 24 and decays linearly over the final six days.
This remains telescoping potential shaping, but selling inventory produces
dense late-season credit instead of relying on one terminal transition.

The in-progress 48-run sweep was spawned before this correction and retains its
old binary and sampled sub-parity asset weights. Its strong candidates remain
useful diagnostics, but its hyperparameter conclusions must not be promoted as
the new default. At 100M steps, run 1 beat the champion 92% over 50 native games
with $21,161 average cash; run 2 won 82% with $17,092. Neither candidate bought
land, and run 2 still lost every game to the $51k native economy bot.

### Corrected continuation sweep

Fresh initialization is not a useful 50M-step hyperparameter objective for
this environment. Three corrected fresh trials finished at only $503-$505,
meaning they had not rediscovered a viable farming policy. Continuing from
`variance_robust_400m.bin` changed the first six 100M-step fixed-panel results
to $17,263, $10,949, $12,022, $5,857, $9,664, and $23,876. This is not merely
a dashboard artifact: the baseline continuation beat the parent champion
44/50 with $17,681 versus $7,153 average terminal cash, and run 5 beat it 50/50
with $25,140 versus $6,836.

The continuation has repaired late liquidation and capital allocation, but it
has not yet learned expansion. Both candidates selected BUY_LAND zero times in
the 50-game champion panel and zero times against the rules bot. A regression
now exercises the complete policy action 75 -> market decoder -> rule-core path
for all three purchases and verifies that all 100 tiles unlock. The mask makes
BUY_LAND legal whenever its price plus the $500 policy reserve is available.
The remaining zero-land behavior is therefore policy exploration/credit
assignment inherited from the parent, not a missing control or simulator bug.

Sweep `score` is mean final money over the configured native fixed panel. It is
comparable between these continuation trials, but it is neither Kaggle rating
nor direct head-to-head win rate. Preserve the top corrected checkpoints and
seat-balance them after the sweep; do not promote a random-init trial or final
checkpoint solely from the live number.

### Primary artifacts

- `logs/kaggriculture/psro_1785727717634_899973120_ranking.tsv`
- `logs/kaggriculture/psro_1785727717634_899973120_confirm_ranking.tsv`
- `logs/kaggriculture/variance_pfsp_peak_confirm_ranking.tsv`
- `checkpoints/kaggriculture/1785727717634/payoffs.tsv`

## 2026-08-03: executable-rule audit

Documentation is not authoritative for Kaggriculture. The pinned competition
interpreter in `kaggle-environments==1.32.2` is byte-identical to
`reference/kaggriculture.py`; differential tests use that executable Python as
the oracle. The README downloaded from Kaggle is newer than the installed
README only in the animal cost/price/timing table, and that newer table agrees
with Python. Several prose statements in both markdown files still disagree
with the interpreter.

The old differential harness was itself broken: its ctypes `KGAction` retained
128 hands after the C ABI grew to 240, shifting `hand_count` and the complete
market queue. It failed on frame one once actually run. The ABI is corrected,
and targeted trajectories now supplement randomized frame comparison.

Confirmed native bugs and fixes:

- A new plant starts with `consecutive_unwatered=1`, not zero. Planting on the
  final turn without watering therefore becomes a weed that night.
- A fed-and-cared animal banks `+1` CARE bonus per day, not the documented
  `+2`.
- On a scheduled production day, a surviving but unfed animal still produces
  the base one unit. It receives no CARE bonus and its bank resets. This is the
  opposite of the README statement.
- DIG is a no-op on an occupied animal structure. Native C previously deleted
  it; the core, trainer mask, and standalone submission mask now reject DIG in
  that state.

Confirmed interpreter behavior that was already native-correct:

- Every surviving animal sets `fertilizer_available=true` at end of day,
  independent of CARE and FEED. It is a Boolean stock capped at one; collecting
  clears it until the next refresh.
- BUY_PRODUCT and SELL both accept fertilizer. A buy/sell round trip is exactly
  reversible if nothing else changes the market.
- Melon starts with one held unit and reaches its six-unit cap at age 10 under
  daily watering. Watering at ages 11 and 12 cannot add yield.
- Strawberry produces at ages 10, 12, 14, and 16 only, then enters decay. It is
  not indefinitely productive.
- The `T` values are fixed price-curve constants. Runtime season length does not
  recompute them; “24-day game” is only the documented calibration rationale.
- Shed access is exactly `(4,4)`, `(5,4)`, `(4,5)`, `(5,5)` on the default map.
  Four early hires spawn in the latter three locked cells then `(4,4)`; the
  unit at `(5,5)` has no legal cardinal move until land is unlocked.

`make -C ocean/kaggriculture parity` now covers planting-day death, full melon
and strawberry lifetimes, animal CARE/fertilizer/unfed production, fertilizer
round trips, locked hand movement, broad scripted play, and 160 randomized
frames. All snapshots match the Python oracle after these corrections.
The parity harness can also consume a Kaggle replay directly. All five public
submission replays reproduced exactly for 719 transitions each, including both
private inventories, farms, market, town, RNG-driven weeds/shops, and money.

The continuation sweep launched before this audit remains loaded with the old
compiled simulator. Its checkpoints were trained with the extra planting grace
day, doubled CARE, missing unfed base production, and destructive animal DIG.
Do not use it for final hyperparameter selection or Kaggle submission. Source
edits do not alter the running process; a normal rebuild is required after the
sweep is stopped or finishes.

### Salvage evaluation of the pre-audit continuation sweep

After stopping that sweep, all candidates were evaluated by the rebuilt,
Python-parity simulator. This is a salvage tournament only: it measures which
old-rule policy transfers best to the corrected game, not which sampled
hyperparameters are optimal for corrected training.

The staged screen covered all 24 completed finals, then quarter-stage
trajectories for the six strongest runs, then a 55--100% zoom over the leading
runs. A final seat-balanced confirmation used 500 games per pair. Its ranking
was:

| policy | cross-play score | mean money |
| --- | ---: | ---: |
| run 5 at 75.37M | 0.6675 | 21,387.5 |
| run 5 final at 99.98M | 0.6430 | 21,486.4 |
| run 9 at 91.75M | 0.5215 | 20,036.6 |
| run 14 at 75.37M | 0.4240 | 19,348.1 |
| run 18 final at 99.98M | 0.2440 | 19,436.8 |

Run 5 at 75.37M beat its final 53.0% over 500 games. That difference is too
small to call a reliable peak, while the final retained more cash against the
fixed rules bot ($21,297 versus $19,635). Treat both as the same strong policy
family. Their action-distribution JSD is only 0.0031. Run 9 is materially more
distinct (JSD about 0.073 from run 5), and run 18 is similarly distinct, so
they are useful diversity candidates even though their aggregate response is
weaker.

None of the confirmed policies beat the rules bot reliably. Run 9 won 1 of 500;
the others won zero. More importantly, expansion is still absent: across 500
games versus rules, run 5 at 75M issued one BUY_LAND, run 5 final issued two,
and run 9 issued one. Each policy issued roughly 226--238k market orders in the
same panel, so the problem is not lack of market-head activity. The inherited
policy strongly explores buying, selling, and hiring while almost never
allocating capital to land.

The best transfer warm start is therefore run 5 final:

`checkpoints/kaggriculture/sweep_1785741903839_0005/0000000099975168.bin`

Run 5 at 75.37M is the close cross-play alternative, and run 9 at 91.75M is the
best behaviorally distinct backup. New training must use the rebuilt parity
simulator. Re-run the sweep from one of these checkpoints before drawing any
hyperparameter conclusion; do not submit these pre-audit models merely because
they dominated the salvage population.

Tournament artifacts:

- `logs/kaggriculture/post_oracle_sweep_finals_*`
- `logs/kaggriculture/post_oracle_sweep_trajectories_*`
- `logs/kaggriculture/post_oracle_sweep_zoom_*`
- `logs/kaggriculture/post_oracle_sweep_confirm_*`

### Land-discovery shaping experiment

The first controlled land experiments confirmed a sharp policy threshold. A
1.1 land-value continuation ended with zero BUY_LAND actions over 200 fixed
rules games. The subsequent 1.2 continuation, run `1785759671182`, restored
moderate expansion without returning to the earlier indiscriminate policy.
Its dashboard averaged 0.129 successful purchases per training-mixture game.
Against rules, BUY_LAND frequency progressed from 0.08/game at 13.11M to
0.125 at 26.21M, peaked at 0.205 at 39.32M, then self-corrected to 0.08 at the
49.97M final while fixed-rules money rose to about $21.2k.

The 500-game corrected-simulator confirmation ranked the no-land branch
`1785758755832@49.97M` first over the small diagnostic population. It beat the
1.2 final 69.6% head-to-head. The 1.2 final nevertheless beat its 1.1 parent
58.2%, the earlier overbuying land policy 83.2%, and the old 658M champion
84.6%. It is therefore the best controlled land-capable response so far, but
not the strongest aggregate policy. Within the 1.2 trajectory, 13.11M had the
best cross-play rank, 39.32M retained the most expansion with strong fixed-rules
money, and 49.97M had the highest fixed-rules money. Preserve all three roles;
do not infer a single winner from the live training mixture.

Artifacts:

- `logs/kaggriculture/land_curriculum_1p2_confirm_*`
- `logs/kaggriculture/land_curriculum_1p2_trajectory_*`

PSRO then screened seven stages from 25--100% of the 1.2 run against all 19
active league members. The 50-game screen ranked 39.32M first; the 500-game
four-policy confirmation gave mean scores 0.5713 (39.32M), 0.5613 (13.11M),
0.5473 (19.66M), and 0.3200 (32.77M). The solved confirmed metagame assigned
98.96% support to 13.11M despite 39.32M's slightly higher population mean,
demonstrating why both aggregate quality and response robustness are retained.
PSRO admitted all four, archived the prior league, and rebuilt a 23-policy
active league with 13.11M as the meta representative. The next land-shaping
taper should explicitly warm-start 39.32M at `reward_land_value=1.15`; using an
unqualified `latest` risks selecting a different experimental branch.

- `logs/kaggriculture/psro_1785759671182_49971200_*`

The subsequent 1.15 taper from the 39.32M checkpoint produced run
`1785761974926`. It did not reduce expansion: the training mixture ended at
0.916 successful land purchases/game, and fixed-rules action probes selected
roughly one BUY_LAND/game through the latter half. This was not purely wasted
behavior, however. Plant actions rose from roughly 3% in the no-land family to
6.1% at 75.37M, showing that the additional area was being worked.

Trajectory evaluation found a severe 26.21M collapse, recovery by 52.43M, a
clear population peak at 75.37M, and regression by the 99.98M final. In the
500-game confirmation, 75.37M beat the strong no-land branch 57.2%, the prior
39.32M land checkpoint 68.2%, and the 13.11M meta checkpoint 69.8%. It had the
best four-policy mean score (0.6507) and money ($21,006). Against the fixed
rules bot it still earned only about $19.2k versus the no-land branch's $20.3k
and prior 39.32M's $21.1k. The checkpoint is therefore a genuine competitive
land-heavy response, not the best fixed-economy policy. Preserve 75.37M and do
not use the 100M final as its proxy.

- `logs/kaggriculture/land_taper_1p15_trajectory_*`
- `logs/kaggriculture/land_taper_1p15_confirm_*`

PSRO over the complete 1.15 trajectory reconfirmed 75.37M as a dominant
response: its 500-game support-set mean score was 0.751 and the solved strategy
assigned it 99.86% mass. It was admitted as the active league meta policy.

A 100M continuation from that exact checkpoint at `reward_land_value=1.10`
produced run `1785765494524`. The lower shaping value retained land behavior
(about 0.79 BUY_LAND/game at its 75.37M stage) but did not improve policy
quality. The best continuation stage lost 61% head-to-head to its unchanged
parent; the 100M final was substantially worse. The iteration chain was stopped
instead of spending another run on a declining branch. This establishes the
current local optimum as `1785761974926@75.37M`, not any 1.10 checkpoint.

The submission archive `ocean/kaggriculture/submission/
pufferlib_land_meta_75m.tar.gz` contains that checkpoint and the standalone
NumPy runtime. A local official-interpreter validation completed with both
agents `DONE` and terminal money $21,238 versus starter's $3,495.

- `logs/kaggriculture/psro_1785761974926_99975168_*`
- `logs/kaggriculture/land_taper_1p10_trajectory_*`

### Kaggle replay audit of the land-meta submission

Public episodes `89735523` and `89735526` exposed the learned failure behind
the land-heavy response. The policy executes the same opening in both seeds:
hire twice, buy melon seeds one at a time, and buy the first $1,000 quadrant on
turn 11 of day 0 while the original quadrant is far from saturated. It ends day
0 with only $518 and 16--17 melons, reaches the $500 reserve by day 2, then
cannot maintain enough daily labor.

The resulting weeds are overwhelmingly failed crops, not random map noise. In
episode 89735523, 38 plants became weeds from missed watering, seven newly
planted crops died that same night without water, and only four weeds spawned
randomly. The agent cleared three and finished with 46 weeds. It watered 318 of
416 end-of-day plant states; 44 misses were already at risk. In episode
89735526, 37 plants died from missed watering, three died on planting day, one
decayed, and only two weeds spawned randomly. It cleared one and finished with
42 weeds. Water coverage was 207/293, with 38 already-risky misses.

The action and capital imbalance is equally clear. Episode 89735523 submitted
254 PLANT commands and 486 WATER commands but only 29 HARVEST commands; 84
PLANT and 116 WATER commands targeted states where they could not productively
execute. It bought 126 seed units and finished with 55 unused. Episode 89735526
submitted 76 PLANT, 232 WATER, and only 11 HARVEST commands. Both games also
bought and resold identical fertilizer quantities and repeatedly bought wheat
product despite running no animals, consuming the single learned market-command
slot without adding value.

The winning crop policy in episode 89735523 demonstrates the missing timing:
it filled the original 25 tiles, maintained seven hands each day, watered
743/757 plant-days with zero neglect deaths, and purchased its second quadrant
only on day 11 after the first melon liquidation. It finished at $38,843 versus
$13,601. The episode 89735526 winner avoided crop maintenance entirely, built
an animal economy, expanded on day 10 after cash flow existed, and finished at
$59,667 versus $14,723.

Therefore unconditional `reward_land_value > 1` teaches the wrong behavior: it
pays immediately for purchasing capacity, not for having the cash, labor, and
watering plan to use it. Returning directly to `1.0` previously erased land
behavior, while 1.10 retained waste and regressed. The next training change
should remove the unconditional land premium and add dense maintenance credit
for watering an at-risk crop, together with diagnostics for neglect deaths,
planting-day deaths, water coverage, and unused seeds. A practical inference
guard should also suppress early expansion until the current quadrant is near
productive saturation and sufficient operating cash remains after purchase.

## V2 reset after Kaggle replay audit

The v2 implementation deliberately breaks checkpoint compatibility. The old
488-byte observation omitted imminent-neglect counters, fertilizer duration,
and pending animal care from policy input. It averaged all hands into three
cohorts and hard-reserved $500 in the learned legality mask. Those choices made
precise maintenance impossible and explain the repeated $500 cash floor.

V2 uses a normalized 1000-byte `uint8_t` observation, eight direct hand heads
plus three scalable overflow cohorts, one sparse market head, and a 604-bit
mask. The structured core still accepts all independent hands and ten market
orders. A ten-head learned market queue was rejected after a smoke test:
inactive slots still contributed flat-PPO entropy and the policy immediately
spent all cash through random orders. Arbitrary queues need an autoregressive
decoder; independent heads are not an acceptable substitute.

The potential discounts an unwatered/unfed at-risk asset by 50%, so WATER/FEED
restores value immediately and a missed refresh incurs loss before destruction.
Raw land returns to exact cost (`reward_land_value=1.0`), purchased assets get a
small holding haircut, and buy-all is excluded from the learned vocabulary
while remaining supported by the simulator. Added telemetry reports water
coverage, neglect deaths, planting-day deaths, unused seed value, and productive
extra-quadrant tiles.

Fresh 1000-byte inputs initially saturated the generic raw-byte encoder: entropy
was zero at epoch one and a two-choice PASS/WATER task never sampled WATER. A
Kaggriculture CUDA encoder now scales bytes by 1/255; the CPU viewer and NumPy
submission adapter apply the same normalization. After this fix, a 2M stage-1
smoke test maintained 100% watering, had zero neglect deaths, earned about $3103
from a $3000 start, and retained healthy entropy around 0.64.

Opening all crop purchases immediately in the next stage still caused a 3M
transfer run to bankrupt and average 26.8 planting-day deaths. The curriculum
was split again: stage 2 now uses a fixed ten-seed crop portfolio with only
SELL/HIRE market operations, stage 3 opens the one-quadrant crop economy, and
stage 4 opens land and animals. A 2M smoke test of the revised stage 2 finished
around $2053, beat the stage-1 policy in 99.6% of stage-2 games, had zero unused
seed value, and reduced ordinary neglect to 0.17/episode. Water coverage was
still only 49%, so stage 2 needs its planned longer run rather than promotion
from the smoke checkpoint.

## V2 curriculum transfer and deployment audit

The first serious v2 run exposed repeated legal-purchase bankruptcy in stage 3:
at 27M it held over $5,000 of unused seeds and only about $150 cash. Seed prices
are fixed, so holding more than two planting turns has no strategic value. The
learned mask now caps pending seeds by empty space and two turns of unit
capacity. Animal purchases are capped by vacant matching structures, wheat by
two feed days, fertilizer by live plant count, and land requires 75% productive
occupancy; weeds do not count as productive saturation. Stage 3 also excludes
BUY_PRODUCT until livestock is available. Native and official parity tests
passed after these changes.

The corrected 15-day crop phase rose from $8.1K at 11M to roughly $15K at 50M,
with 85.5% watering and only about $50 of unused seeds. Extending it directly
to 30 days was essential: the 75M long-season run reached 92% watering, about
five neglect deaths, and transient training scores near $29K. Directly opening
land or livestock still damaged the policy because masked decoder rows had
never received gradients and the flat board encoder did not generalize to 25
new empty tiles. `checkpoint_gate.c` now initializes newly legal rows from the
same head's trained PASS row with a negative offset. The unrestricted track is
kept separate from the strongest crop specialist until it proves better in
fixed evaluation.

The long-season curriculum originally still granted one free seed of every
crop, unlike the official zero-seed reset. Under the official/full reset that
policy averaged only about $7.6K against native rules. A 50M response trained
on stage 4's real zero-seed opening averaged about $22.5K during training and
$22.7K in a 50-game native full-mask benchmark. Its first checkpoint was more
robust than the final: over 100 games against rules it averaged $23,727 and beat
the 50M final 78–22. It is archived as
`saved/kaggriculture_v2/champion_crop_real_opening.bin`.

Submission inference must remain stochastic. Masked argmax averaged only
$2,849 across five official-kit seeds. The old fixed policy RNG seed 73 ranged
from $1,671 to $17,653; a small official-kit screen found seed 97 stable from
$18,927 to $19,787 (mean $19,357) across the same five seeds. The submission
adapter now defaults to policy seed 97, and the rebuilt archive is
`pufferlib_kaggriculture_v2_crop_champion.tar.gz`.

## Sweep protocol after the v2 reset

Hyperparameters and reward coefficients are searched sequentially. A joint
search would currently have 19 active dimensions; 48 trials would provide too
little coverage and would confound optimizer quality with reward semantics.
`sweep_v2.sh hypers` searches 12 PPO/EMAg dimensions for 48 warm-started 25M
trials with rewards and opponent composition fixed. `sweep_v2.sh rewards` then
imports the selected run's hyperparameters and searches seven crop-relevant
reward dimensions for 40 trials. Run zero is the configured baseline, about ten
initial samples establish random coverage, and the remaining trials are guided
by the GP/Pareto optimizer.

Exploratory trials use the mixed training score directly. A smoke test showed
that a five-policy post-run tournament took minutes even after a 1M-step trial,
so evaluating every candidate that way would dominate sweep cost. The compact
ranked report retains each run's INI and final checkpoint;
`eval_sweep_v2.sh` performs seat-balanced cross-play and native-rules evaluation
only for the top candidates. Final selection still requires 75–100M reruns of
the top three or four configurations across multiple seeds.

The first 48-run v2 hyper sweep completed on 2026-08-03. Its raw top ten were
separated by only 1.3%, and the raw winner did not survive cross-play. A
seat-balanced tournament of the top eight plus three v2 baselines, with 100
games per pairing, ranked the unchanged crop champion first at 0.597 mean
score. Run 31 was the strongest trained continuation at 0.5795; the raw winner
fell to 0.496. The reward sweep therefore starts from the unchanged champion
but imports run 31's optimizer settings, archived in
`saved/kaggriculture_v2/selected_hypers_20260803.ini`. This preserves the best
policy state while testing the most robust new optimizer configuration.

## 2026-08-04: compact controls and normalized spatial routing

The attempted ten-head learned market queue was not a Conditional Action Tree.
It emitted ten independent 76-way distributions every turn, so PPO paid entropy
for inactive slots and a fresh policy sampled a destructive volume of unrelated
orders. Conditional masking cannot be recovered by merely splitting operation,
item, and quantity into ordinary Puffer heads because those heads are sampled
simultaneously. A true CAT needs an autoregressive sampler and the summed
log-probability/entropy of only the visited path.

The practical current interface uses one 22-way market intention: PASS,
one-unit buys, sell-all by product, HIRE, and BUY_LAND. Repeating an intention
over turns expresses quantity. This is deliberately a small Gameboy-style
policy vocabulary; the native simulator and scripted bots retain arbitrary
quantities and all ten ordered market operations. Purchase masks preserve a
cash reserve, cap pending seeds to one planting turn, require feed for bought
animals, cap hires by available productive work, and expose land only when the
current field and workforce can support it.

Two observation defects were more serious than model size. The CUDA encoder
divides bytes by 255, but positions, day/hour, entity IDs, ages, and counts had
been written as tiny raw integers. The network therefore saw most spatial state
near zero. Market inventory was written as an absolute value near 10,000 and
saturated, hiding supply/demand direction. The 1156-byte ABI now scales each
field over its useful range, encodes market deviation from I0 using resource
throughput, and gives every controlled unit egocentric signed offsets to the
nearest maintenance job, harvest, empty tile, weed, and shed. Full public boards
remain present and opponent private inventory remains absent.

The inactivity hint from another competitor was implemented as a sweep metric,
not as fake profit: an episode ending within two coins of starting money reports
`sweep_score=-3000`, while `score` remains actual money. PPO also receives a
small terminal inactivity penalty. Productive-action shaping excludes movement,
buying, planting, and building so it cannot directly reward purchase spam.

Early probes establish an important checkpoint-selection rule. The egocentric
full-rules behavior run jumped to about $811 by 2.5M, versus roughly $128 before
spatial routing, but its shaped objective then overtrained. A 6.55M checkpoint
was more robust in cross-play than the 9.83M final. An economic continuation
improved watering from about 0.27 to 0.45 and cut planting-day deaths from about
19 to 8, yet money peaked near $755 and ended near $620; fixed-panel mean money
was $621. It learned chores rather than profitable farming and is not admitted
to a league. Behavior bootstraps are therefore capped near 6.5M and continuation
reduces productive-action credit to 0.0002. Final checkpoints must never be
promoted without intermediate, seat-balanced evaluation.

The 0.0002 continuation subsequently validated that decision. A 30M probe rose
steadily under native fixed evaluation even while dashboard money was noisy.
Continuing its best 22.94M snapshot for another 70M produced a smooth economic
trajectory: fixed rules money rose from $1,008 at continuation step zero to
$6,113 at 19.66M, $9,817 at 42.60M, and $11,258 at 65.54M. The corresponding
pass money was $737, $5,313, $10,753, and $13,308. The 65.54M snapshot beat the
final 70M snapshot and is archived as `saved/kaggriculture_v3/profit_65m.bin`.
Five trajectory points seed `saved/kaggriculture_league_v3`; they are diversity
opponents, not yet a solved PSRO distribution.

The trace still exposes the next bottleneck: at 65.54M WATER is 15.6% of unit
actions but HARVEST only 0.8%, and expansion remains nearly absent. The policy
is now economically useful enough to train against, but animal production,
efficient inventory return, and land should be taught by stronger native
behavior opponents rather than free assets or unconditional land reward.

## 2026-08-05: diagnostics for V4 failure analysis

The generic dashboard was hiding the useful distinction between spending and
production. Kaggriculture now accumulates market queue depth and order types
(buy, seed, product, animal, sell, hire, land), animal place/feed/care/harvest/
fertilizer actions, and terminal plants/animals/weeds. `orders_per_turn` is
derived from the episode queue count; all detailed counters remain in the raw
`env/*` log. The visible panel is specialized to money/result, expansion,
crop health, animal lifecycle, queue activity, and opponent mix. This makes a
run that buys land and leaves $6k of unused seeds distinguishable from one that
actually expands productive capacity.

The native evaluator also reports `orders/turn` and the major order/action
counts in its per-policy line. `eval_v4.sh` records those columns alongside
rules/pass money, so checkpoint selection can reject policies that look good
only because they spend aggressively or never use animals.

## 2026-08-05: V4 conditional queue and semantic state

The one-order V3 policy was not the game: Kaggriculture permits ten ordered
market operations in one turn, and strong public agents use that queue for
coherent buy/hire/build openings. V4 implements the missing Conditional Action
Tree end to end. Twelve unit heads are followed by ten three-head market slots:
STOP/CONTINUE, one of 21 operation/item commands, and one of eight quantities
`1,2,3,4,5,6,8,10`. Ten slots can compose every integer quantity through 100,
without making destructive 32/64/ALL purchases common under a fresh policy.
The CUDA sampler applies conditional visitation across these heads. PPO,
entropy, KL, EMAg, and gradients include only visited branches. A stopped
suffix and the quantity child of HIRE/BUY_LAND contribute exactly zero. This is
838 logits, but never a flat 25K action distribution and never 30 independent
random decisions.

Visitation is conditional, but generation is not yet truly
autoregressive. The fused decoder emits every slot's logits from the same
pre-action recurrent state; a later slot does not receive earlier sampled
orders as input. STOP only controls whether those already-emitted child logits
are visited. This distinction must be retained in future CAT/JSD work.

Masks now represent simulator legality rather than strategy. Cash reserves,
seed caps, structure/feed prerequisites, desired-workforce formulas, and field
occupancy thresholds were deleted. Those rules had made strong same-turn plans
unrepresentable—for example buying an animal before building its structure.
SELL remains exposed because an earlier order in the same queue can create
stock; invalid realized orders remain the official silent no-op.

The observation changed from a dense ordinal board dump to a 1024-byte semantic
ABI. Farms cannot physically interact, so global public state is encoded as
per-quadrant entity/lifecycle aggregates and per-product crop/animal lifecycle
summaries. Own unit/cohort slots retain exact inventory, one-hot local entity,
status, and egocentric route offsets. Shared market/town state and opponent
public positions remain; opponent private inventory remains hidden. This gives
the MLP categorical and strategic variables directly while preserving the
navigation information needed by each action head.

A low-rank shared worker/market decoder was also implemented and measured. It
saved about 14K parameters but split one fused decoder GEMM into several small
GEMMs and made training slower, so it was removed. V4 keeps the default fused
decoder. Native adapter tests pass, CUDA builds cleanly, and smoke PPO statistics
are finite. A topology smoke also found that native bot overrides require
selfplay enabled with at least one physical opponent bank; zero banks silently
creates learner mirror games regardless of bot fraction. The default V4 config
therefore starts fresh with one bank, no external V3 pool, and 100% rules-bot
opponents. V4 must build its own league because old checkpoints have incompatible
observation, head, and decoder dimensions.

## 2026-08-05: structural ablation exposed a mixed hyperparameter regression

A fresh 256x2 policy with one usable market slot, at most eight hands, horizon
128, and terminal-only reward did not learn an economy. Fixed 100-game checks
put both its 26M and 50M checkpoints at about $1 against rules and pass. Adding
a conservative `0.0001` economic potential improved the 50M checkpoint only to
$33 against rules and $21 against pass. Training win rate was misleading because
the historical mirror population learned the same bankruptcy convention.

The run made only 190 rollout refreshes in 50M transitions: 2048 agents times a
128-step horizon delays each acting-policy refresh until 262,144 transitions.
The successful v3 setup used horizon 16, or one refresh per 32,768 transitions,
and showed about 3051 rollout epochs per 100M. The failed V5 file also mixed LR,
entropy, EMAg, and reward values from unrelated large-model, CAT, reward-sweep,
and PSRO experiments. The next baseline restores the coherent v3 continuation
tuple (`lr=0.0002`, `horizon=16`, `ent=0.003`, EMAg `0.03/0.001`, economic
potential `0.000772047148`, outcome `0.375989795`) while keeping only the
one-slot/eight-hand structural ablation and corrected simulator as new factors.
## 2026-08-07: byte normalization moved into the generic transfer

The Kaggriculture CUDA encoder used to run `kaggriculture_scale_bytes` (a
1/255 scale kernel) on every policy bank's input during forward, with a
dedicated `scaled_input` scratch tensor per bank. The generic byte-to-bf16
transfer in `src/pufferl.cu` now performs that same `byte * (1/255)` once,
when the shared observation buffer is copied into the rollout buffer.
Kaggriculture enables it with `PUFFERLIB_OBS_U8_NORMALIZED` in
`kaggriculture.h`; the core change is a four-line `#ifdef` around that one
cast kernel and no other environment defines the macro, so non-Kaggriculture
builds compile the original `dst = (float)src` line. The env-local scale
kernel and its scratch tensor are deleted and the encoder weight gradient
uses the already-normalized `saved_input`, so training math is unchanged.

This was a shared-scale win (one scale per step instead of one per bank) but
required a compile-time hook in core, which the project generally avoids.
Measured impact on SPS was within run-to-run noise, so it was kept only as a
small cleanliness/per-bank-launch saving. The revert path is to restore a
scale kernel in `kaggriculture.cu` and drop the macro.

## 2026-08-07: env hot-path profile shows observation encoding, not the sim

`bench_hotpath` plus targeted timers on a busy farm (9 units, ~34 plants)
show where the CPU env time actually goes per two-player step:

| Component | Cost |
|---|---|
| `kg_step(PASS)` | 0.01 us |
| `kag_write_mask` | 0.17 us |
| `kag_bot_action` (rules) | 0.93 us |
| `kag_write_all_observations` | 17.9 us |
| full `puf_step` (no bot) | ~21 us |

Observation encoding is roughly 85% of the env step, and route encoding is
roughly half of that: 8.7 us of the 17.9 us comes from
`kag_encode_unit_routes`, which scans all 100 tiles once per unit head (12
heads per player) for the nearest maintain/harvest/empty/weed target.
`kag_collect_farm_summary` is only ~1 us and the market-mask writer is free.
The core game step itself is nearly free, so "the sim is slow" is not the
problem: the adapter's O(unit_heads x board) observation writer is.

Implications for a GPU environment port: a CUDA port would mostly be moving
this encoder (plus the ~1-3 us real step, market, and bot logic) onto the
GPU. The cheaper first move is to make the encoder O(board + units): compute
a Manhattan distance transform per route class once per farm per step and
let each unit/cohort do O(1) lookups, which should cut obs cost from ~18 us
to roughly 5-7 us on a busy farm. Only if env still dominates after that is
a full GPU port worth its complexity (one-warp-per-game serial market
processing, bot ports, parity, and new sync points).

## 2026-08-07: route fields now use per-farm distance transforms

The route bytes (`maintain/harvest/empty/weed/shed` nearest targets) were
rebuilt as four Manhattan distance transforms per farm per observation write.
`kag_build_route_table` seeds each source cell with its absolute coordinate
and a distance byte, runs the standard forward/backward two-pass relaxation,
and `kag_encode_unit_routes` does O(1) per-unit lookups. Tie-breaking compares
the stored source tile index, which reproduces the old first-tile-in-scan-order
choice byte-for-byte; route bytes are unchanged and existing checkpoints stay
valid. `puf_step` still writes one table set per player; tables are not yet
cached across the two players' observation writes.

Measured on the busy hot-path farm (9 units, ~34 plants), `kag_write_observation`
dropped from 22.39 us to 6.39 us, `write_all_obs+masks` from 22.52 us to
12.48 us, and `puf_step(+rules bot)` from 28.36 us to 14.90 us. The Python
parity suite still passes (randomized 160-frame, animal CARE/fertilizer,
documented edges, and the 48-frame fixed-seed suite), and the CUDA
`./kaggriculture` binary rebuilds cleanly.

## 2026-08-08: exact magnet KL, reach-aware JSD, and faster probes

The EMAg KL penalty is now the exact conditional-tree objective. The PPO
kernel computes a per-head magnet reach weight (flat envs return 1; the
Kaggriculture market chain multiplies the magnet's STOP/CONTINUE softmax
probabilities, and quantity heads additionally weight by the magnet's
probability of choosing a quantitative command) and uses it for both the KL
value and its gradient. KL and gradients now also flow into heads the magnet
reaches but the rollout did not, which the old rollout-reachability
approximation omitted. The generalized hook lives in `src/algo.cu`
(`puf_head_reach_weight`) behind the same `PUFFER_CONDITIONAL_*` constants
already used by `puf_action_head_active`; other environments keep a flat
reach of one and are bit-identical.

The JSD probe now compares heads by executed-policy reachability instead of
raw head space. Each policy's own masked softmax supplies its continuation
probabilities, and a pairwise head contribution is weighted by the product of
the two policies' reach probabilities, so unreachable market tail logits stop
inflating the matrix. `KAG_JSD_SEEDS` (1-32) runs the probe over independent
deterministic seeds instead of replaying one seed-42 trajectory, and the
default probe length is one full 720-step episode per seed.

PSRO defaults moved to `--jsd-steps 720` and `--jsd-seeds 2`: the same wall
time as the old 1440-step single-seed probe now covers two distinct
trajectories, and the reach weighting removes tail noise on top. Exact EMAg
was smoke-tested with a 160-epoch, 61.4K-step training run: finite losses,
healthy entropy, and no NaN. The temporary smoke run, its log, and two empty
payoff dirs were removed afterward so the running PSRO pipeline still sees
its own newest run.

## 2026-08-09: GPU-native league evaluation

`eval_population.sh` now uses the CUDA `puffer match` path by default. Each
model pair is evaluated in both seats, with the two results combined into the
same payoff, money, and matrix TSVs consumed by the existing metagame solver.
Fixed `pass` and `rules` opponents use the CUDA `eval_bot` path. `--cpu`
remains as an explicit compatibility fallback, but PSRO no longer uses it.

The matcher emits a machine-readable `match_result` line containing games,
win rate, draw rate, and both final money values. Match batching no longer
collapses small requested game counts to an invalid one-buffer layout; the
configured evaluator batch may intentionally overshoot the requested count so
the GPU stays full. Pair seeds are offset by policy indices so separate
matchups do not all reuse one deterministic trajectory.

`profile_population_gpu.sh` replaces the old bench-only PSRO profile. It
records win/money, water/neglect, plants/animals/weeds, and market activity
from native `eval_bot` JSON without adding counters to the training hot path.

The default Kaggriculture config now uses 64 eval agents for short dashboard
and self-play-panel checks; the old zero/8192 defaults made a 10- or 50-game
check silently launch a much larger resident batch. `eval_v4_parallel.sh`
and the population/PSRO scripts expose the batch explicitly through
`KAG_GPU_AGENTS`/`--gpu-agents` (64 is the current safe default). The model
forward pass is CUDA-backed, while Kaggriculture's state transition and
observation writer remain CPU-native (`vec.gpu_env=0`); this is not a claim of
a CUDA simulator port. Because the environment is vectorized, a requested
short check is a lower bound on completed episodes; the raw `match_result`
line reports the actual count, and the GPU profile TSV calls this
`games_completed`.

Named `pass`/`rules` match opponents now activate the Kaggriculture bot lane
instead of being interpreted as a missing frozen model. Eval logs distinguish
`mirror_fraction`, `rules_fraction`, and the new `pass_fraction`, so a fixed
bot check cannot masquerade as mirror self-play. Fixed-opponent population rows
also run the model once in each seat (`env.bot_first=0/1`) and average the
model-perspective result; model-vs-model rows were already seat-balanced.

## 2026-08-09: compact persistent league and incremental PSRO

The process-per-pair population evaluator was the real PSRO bottleneck. A
26-policy screen meant 325 unordered pairs, two seat directions, and 650 CUDA
process/model initializations. Small `--games` values were also silently
overshot by the resident evaluation batch. That design is retired for compact
populations.

`./puffer league kaggriculture` now creates one vector batch, keeps up to eight
frozen opponents resident, alternates learner seat within each opponent tag,
and reloads only the learner/opponent weight buffers between waves. It records
exactly the requested even game count for every tag. This is persistent CUDA
policy inference over the native CPU simulator; it is not a CUDA simulator
port or a grouped-GEMM fusion of different networks.

The active V6 league is capped at four policies. Its standalone `league.ini`
contains one `[policy.NAME]` section per checkpoint with `path`, `role`,
`train_weight`, and `enabled`. Training reads these weights natively and PFSP
multiplies them by online matchup priority. The unlimited archive remains on
disk but is no longer loaded into every training/evaluation process.

PSRO now uses a coarse-to-fine search rather than pretending a non-monotonic
training trajectory can be binary-searched. Twelve evenly spaced checkpoints
are screened against the four active policies using the league's configured
base weights, the best four local peaks enter the full matrix, and at most two
are admitted. Active-active payoff rows are cached by checkpoint SHA-256 plus
the evaluator binaries/stage/reset context, so later iterations evaluate only
candidate-led pairs without reusing rows across rules or curriculum changes.
The first cache fill is a complete matrix; subsequent iterations reuse the six
active-tail rows. Architecture-incompatible files inside a reused run directory
are ignored before screening. The manifest's champion is pinned through the
four-policy prune; all other policies remain replaceable.

Default PSRO cost is now 12 coarse candidates, four shortlisted candidates,
10 games per screen-matrix pair, and 50-game confirmation. Fixed-bot profiling
is opt-in. A smoke iteration including compilation completed in under a minute
on the development machine, versus more than an hour for the old 26-policy
process farm. The normal commands are:

```bash
./ocean/kaggriculture/psro.sh analyze --run checkpoints/kaggriculture/RUN
./ocean/kaggriculture/psro.sh iterate --run checkpoints/kaggriculture/RUN
./puffer train kaggriculture
```

Sweep bounds were widened to contain the actual configured defaults (including
LR, horizon, and EMAg tau), and the generic sweep error now names an offending
key/value instead of only asserting.
### Land-expansion ablation against the top hybrid (2026-08-09)

`BUY_LAND` remains present in the learned policy's market command head and is
masked legal at the exact $1k/$2k/$4k prices. The top hybrid almost never uses
its older day-4/day-9 heuristic because its animal opening spends below the
heuristic's $1.5k/$3k cash thresholds.

A seat-balanced 1,000-game native ablation made land purchasing continuous,
cash-reserved, capacity-gated, and protected from market-queue crowding. The
first version averaged 2.05 expansions and lost every game to the unchanged
hybrid (money 13,032 vs 22,672). Restricting it to exactly one expansion still
won only 22.5% (20,082 vs 22,376); raising the retained cash reserve to $4k
won 19.2% (20,151 vs 22,643). These variants were reverted. With the present
planner and market balance, buying space causes excess seed/hand expense and
sell-pressure faster than it produces profit. Future land work should be a
separate diversity/oracle branch with demand-aware production caps, not a
change that weakens the champion opponent.

## 2026-08-09: GPU-resident Kaggriculture simulator

The Kaggriculture environment is now a full CUDA port (`kaggriculture.cu`),
not just a CUDA policy over the CPU simulator. `vec.gpu_env=1` keeps the
`Env` array device-resident: reset, scripted-opening execution, the
state transition, observation/action-mask writers, reward computation, bot
lanes, and the top-bot tape path all run on device. The policy encoder
(`kaggriculture_encoder.cu`) is included after the generic policy types exist,
so the env hooks and encoder stay separate translation fragments.

Parity is proven adversarially rather than by a few happy paths:

- `test_cuda_core.cu`: 32 configs x 720 exact turns of random+hostile action
  fuzzing (weed spawn 0%/100%, $0/$500/$3000+ starts, shed capacity 1/7/100,
  market-order limits 1..10, town intervals swept) with byte-exact `KGState`
  comparison after every turn. PASS.
- `test_cuda_adapter.cu`: 12 adversarial modes x 1440 turns over the full
  policy adapter (state, boundary, observation, action mask, reward,
  terminal, potential, episode return, and all log fields), covering every
  scripted/adaptive bot, opening-mask and reset-opening handoffs, reward
  shaping on/off, and bot-seat swap. PASS.

`make cuda-core cuda-adapter` builds both with the local CUDA 12.6 toolchain
(`build.sh` already falls back to `/usr/local/cuda/bin/nvcc` when `nvcc` is
not on PATH). The GPU sweep (`gpu_env=1`) runs at ~160K SPS with the env step
down to ~34% of frame time and GPU at 99%; the model/train path is now the
bottleneck, not the simulator.

## 2026-08-11: BC pipeline and champion-against-anchor plan

Added a standalone CUDA behavioral-cloning pipeline (`ocean/kaggriculture/kag_bc.cu`):

- `bc.mode=gen` runs seeded self-play/mirror games with a chosen bot and writes
  every step's (obs, expert action-heads, packed mask) to a binary dataset.
- `bc.mode=train` loads the dataset and minimizes masked cross-entropy over the
  policy's fused decoder with host-side SGD (float master + bf16 refresh,
  momentum, per-reg grads). Output is a standard flat checkpoint usable as
  `base.load_model_path` or as a frozen EMAg magnet via `selfplay.magnet_path`.

Baseline vs the top hybrid (bench, model P0):

- Prior champion `sweep15_h128_14256`: avg money 4,718 vs 28,512 top. Still the
  best model.
- BC anchor (300 games of top-vs-top mirror, 128-wide, CE 0.0077): 0 vs 32,933
  top. It overbuilds pastures and buys seeds but never places/feeds animals, so
  it goes bankrupt.
- PPO finetune of the anchor (50M steps, anchor as magnet): 0 vs 33,372 top.

Root cause of the missing animal loop in the data: `KG_SCRIPT_TOP`'s t>=26 path
delegates to `kag_bot_action` (crop economy only); the animal actions seen in
bench "top" are just the t<26 tape opening. Full animal-maintenance bots are the
adaptive profiles (`KAG_ADAPTIVE_STRUCTURED`=7 via `bc.bot=2`, HARVEST_PULSE=6
via `bc.bot=3`, and FIELDS/KAITO which build+place+buy animals).

Branch being tried next: PPO warm-started from the prior champion with the BC
anchor as the frozen KL magnet (paper Step 2), moderate emag_kl_coef, mixed
opponent lane to avoid the bankruptcy collapse, then evaluate against the top
hybrid and the champion.

## 2026-08-11: Exp A beats the prior champion

Exp A: PPO from `sweep15_h128_14256` (prior champion) with the top300 BC anchor
as frozen magnet. LR 5e-5, ent 0.0005, vf_coef 0.02, vf_clip 0.1,
emag_kl_coef 0.01, 25% top hybrid / 75% rules econ lane, 4096 agents x 64,
120M steps. Promoted to `saved/kaggriculture_hall_of_fame/expA_champ_magnet_120m.bin`.

Results (bench vs top hybrid, 2000 games):

- ExpA final: avg money 7,576 vs 30,702 top (prior champion was 4,718).
- ExpA vs prior champion head-to-head: 14,001 vs 11,645 (ExpA wins).
- Trajectory vs top: 52M=5,453, 78M=7,290, 104M=6,244, 120M=7,576 (final best).

Key behavior change vs the old champion: much higher order volume (sell 1,369,
hire 393, buy 305 vs champion's sell 1,160/hire 283/buy 278). Still no animal
place/feed/care loop.

Branch B (next): continuation from ExpA with a NEW anchor trained on the
adaptive structured bot (KAG_ADAPTIVE_STRUCTURED=7), which maintains a real
animal economy. The magnet should pull the policy toward feed/care/harvest/
collect and animal placement, adding the missing production loop on top of
ExpA's strong crop economy.

## 2026-08-11: Exp B (animal anchor) - mixed result

Exp B: PPO continuation from ExpA champion with the animal-economy anchor
(adaptive structured, 287.6k-step dataset, CE 0.0179) as magnet,
emag_kl_coef 0.02, ent 0.001, 150M steps, 25% top lane.

- vs top hybrid: 6,990 (ExpA was 7,576) - slightly worse.
- vs ExpA head-to-head: 13,579 vs 11,138 - ExpB WINS the direct match.
- Still no animal place/feed/care actions in eval.

Interpretation: the animal anchor pull added strategic diversity that beats
ExpA in the mirror, but emag_kl_coef 0.02 was too strong against the top
hybrid (lower vs-top money). The animal loop still did not emerge. Two
follow-ups: (a) lower emag with the animal anchor to keep the head-to-head
edge without losing vs-top, and (b) a longer plain continuation of ExpA with
the original crop anchor, which was still improving at 120M.

Exp C (next): continue ExpA champion with the crop anchor magnet (the config
that produced the best vs-top result), 200M more steps.

## 2026-08-11: Exp C new champion (320M total)

Exp C: ExpA champion + 200M more with the crop anchor magnet (same config:
LR 5e-5, ent 0.0005, emag_kl 0.01, 25% top lane). Promoted to
`saved/kaggriculture_hall_of_fame/expC_champ_320m.bin`.

- vs top hybrid: 7,766 (ExpA 7,576; prior champion 4,718).
- vs ExpA head-to-head: 13,306 vs 12,369 (ExpC wins).
- First hints of animal behavior in eval: animal_place=1, care=2,
  fert_collect=1 (still negligible, but non-zero for the first time).

Exp D (running): continue ExpC another 250M with the same recipe to see if
the vs-top curve keeps climbing. Branch options after: (a) harder lane (50%
top) to sharpen the actual target, (b) animal anchor at low emag (0.005) to
add the missing production loop, (c) higher LR from the current champion.

## 2026-08-11: Exp D peak checkpoint is the new champion

Exp D (ExpC + 250M, same config) did NOT keep climbing to the end: final 250M
checkpoint scored 7,884 vs top and LOST to ExpC head-to-head (10,046 vs
18,834). But the trajectory peaked early: the 20M checkpoint hit 10,050 vs top.

Promoted `kag_expD_champ_cont/0000000020971520.bin` (20M) to
`saved/kaggriculture_hall_of_fame/expD_peak_20m.bin`.

ExpD@20M results:
- vs top hybrid: 9,484 (prior champion 4,718, ExpA 7,576, ExpC 7,766,
  ExpD-final 7,884).
- vs ExpC: 13,399 vs 9,114; reverse seat 13,041 vs 12,810 (ExpD@20M wins both).
- vs ExpD-final: 13,182 vs 12,026 (wins).

Pattern: warm-start continuation improves sharply in the first ~20M then
decays, matching the "final is not best" PSRO lesson. Short finetune spikes
are the productive regime.

Exp E (next): short continuation of ExpD@20M (20-30M) to see if the spike
pattern repeats and produces another step up. If it decays again, the
checkpoint-mining approach (screen mid checkpoints after each short run) is
the reliable promotion path.

## 2026-08-11: eval methodology correction

`kaggriculture bench N` steps N total frames, so `bench 2000` was only ~2.7
games. Early "9,484" and "11,300" claims were 2-game noise. Correct protocol:
`bench 72000` = 100 games/side.

Corrected 100-game vs-top results:
- prior champion sweep15: 6,871
- ExpD@20M: 7,102
- ExpF@5M (50% top lane, 30M run peaked at 5M): 7,485

Head-to-head ExpF@5M vs ExpD@20M (100 games both seats): 12,321/11,424 vs
11,267/12,417 — essentially tied; ExpF's vs-top edge is modest but real.
All three still lack the animal production loop. Next lever is structural,
not more warm-start continuation: the animal-anchor at low emag, or a
mixed-dataset BC anchor (crop + animal bots) trained at high capacity.

## 2026-08-09: GPU reset diversity and selfplay rotation fixes

The CUDA reset kernel was re-deriving `reset_opening_rng` from the per-match
seed inside a `memset(env, 0)` path, so every full reset replayed the exact
same opening prefix for each match. The rng now lives in a persistent device
array (`d_kag_opening_rng`) that is seeded once per match and advanced by both
the reset kernel and the in-transition auto-reset, so `reset_opening_turns`
actually samples fresh prefix lengths across episodes on the GPU sim.

GPU selfplay rotation was silently disabled: `PUF_GPU_SELFPLAY=1` skipped the
assert while the bank counting and rotation loops were CPU-only, so
`opponent_pool_prob`, PFSP, and `opp_timeout_steps` were no-ops against the
four fixed league banks. Rotation is now host-driven from per-bank
completed-episode counters (`d_kag_bank_completed`, incremented on terminal
transitions and cleared on bank load/timeout), with per-bank env counts taken
from `bank_layout`. `vec_log_tag` already worked on GPU, so the league payoff
logging block is enabled there too.

Cheap hot-path reductions: bot/script/public action writers now initialize
only `hand_count`/`market_count` instead of memsetting the full 3.3 KB
`KGAction` (only initialized fields are ever read by `kg_step` and the market
processor), and the observation writer is dense: skipped regions write their
zeros explicitly and only the final padding tail is memset, cutting the
per-row pre-zero from 1024 bytes to ~80. Init-time CUDA allocations and the
`__constant__` config upload now fail loudly with the offending call named.

Still structural and deferred until a build/test cycle is available: splitting
the serial per-match obs/mask writer into a state kernel plus an
element-parallel row writer, batching frozen-bank forwards into a grouped
GEMM, widening the `sample_logits` head cache, and folding the env step kernel
into the fused rollout cudagraph.
