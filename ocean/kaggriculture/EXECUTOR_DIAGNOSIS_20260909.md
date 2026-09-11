# Executor and observation diagnosis — 2026-09-09

Read-only diagnosis of the **remote** macro-mode-2 implementation. No production
code, config, league, running trainer, or submission was changed. Diagnostic
sources and a separately compiled shared library live under
`/workspace/PufferLib/logs/kaggriculture/executor_audit_20260909/`.

## Main conclusion

There are concrete execution/abstraction problems worth addressing before an
encoder rewrite or another reward sweep. This is not evidence that fixing them
will yield $100k, nor that all policy choices are sensible. In particular, the
policy frequently selects a legacy whole-farm strategy, not explicit crop and
animal intents. That legacy strategy omits newer automatic livestock placement.

## Evidence and scope

Traced first 300 turns of eight root-start episodes: two seeds (91001, 91002),
both seats, two policies, against the same saved opponent. Policies:

- Parent: `run_fuzzy_phases_v2_t1_a0_0000000298844160.bin`.
- Candidate: PSRO-selected `fresh_phases_fixed_v2_latest_20260909_1618`
  checkpoint `0000000044040192.bin`.
- Opponent: `run_expansion4_obs1_20260907_EMAG_psro_response_0000001082130432.bin`.

Native remote observations/masks/executor/core; NumPy recurrent inference;
stochastic masked choices; persistent macro context and recurrent state.
These are **CPU diagnostic games, not replacement GPU PSRO scores**. Small
sample, one opponent; no claim of population-wide frequency or cash uplift.

The unit phase was separately replayed on disposable native states, including
the atomic seed-demand check. All 13,321 emitted non-PASS unit commands changed
player state. This rules out silent invalid unit commands in this sample, not
bad allocation, unprofitable actions, or all possible market failures.

## 1. DIVERSIFY bypasses the newer automatic livestock-placement path

Remote `kaggriculture.h:4373` calls `kag_bot_action`, which uses
`KAG_BOT_JOBS_ALL` (MAINTAIN/HARVEST/DIG/PLANT), **without OPERATE**.
The stock-to-empty-housing placement jobs and automatic animal pickups in
`kag_bot_jobs_ex` / `kag_bot_action_filtered_ex` require OPERATE. Most other
structured macros use it through `kag_macro_operate_action`.

| First 300 turns, four episodes per policy | Parent | Candidate |
| --- | ---: | ---: |
| DIVERSIFY selections / 1,200 decisions | 378 | 719 |
| Turns with unplaced cow stock | 247 | 573 |
| DIVERSIFY selections while cow stock waits | 34 | 337 |
| Cow PLACE commands under DIVERSIFY | 0 | 0 |

Candidate cow backlogs remain continuously nonempty for 153–154 turns in three
episodes. This is a backlog duration, **not identification of one particular cow**.
Carried cows also compete with chores; background priorities can redirect their
workers to watering, feeding, care, and fertilizer collection.

Minimal fixture: a worker carries a cow while standing on a compatible empty
pasture. HOLD emits PLACE; DIVERSIFY emits PASS. Both are evaluated on the same
unchanged state. Native PLACE succeeds. This confirms an execution inconsistency,
not simply failure to buy cattle.

## 2. DIVERSIFY is still a full scripted strategy

It independently chooses crops via `kag_bot_crop_rank` / `kag_bot_crop`, hires
toward 4/8/12 hands, and buys land on `game->day == 4 || game->day == 9` while
total plots are below three. It also uses the legacy harvest timing.

In three parent episodes, both land purchases occur at turns 216 and 217 under
DIVERSIFY, exactly within its day-9 schedule. Across all eight traced episodes,
neither model selected explicit PLANT_STRAWBERRY even once. Their strawberries
come from the generic plan. The candidate also never selected PLANT_MELON;
its melons come from that plan too.

This is a plausible explanation for repeated similar openings and sensitivity
to rewards that change how often DIVERSIFY is selected. It is **not a global
two-purchase simulator cap**: explicit EXPAND can buy the fourth plot.

Simply masking/remapping DIVERSIFY on existing models would change their action
contract. Do not silently do that to the current league or packaged champion.

## 3. Targets are truncated before worker-distance assignment

`kag_bot_jobs_ex` chooses a row-major prefix of empty cells limited by requested
plant quantity/available seeds. `kag_macro_selected_animal_action` similarly
chooses a row-major prefix of compatible empty housing limited by batch stock.
Only then does the worker assignment choose among those targets.

Reproduced fixtures:

- PLANT_MELON(1), one seed, worker on empty (4,4), empty (0,0): emits WEST toward
  (0,0), not a legal immediate PLANT at its own position.
- COW(1), cow already carried, compatible empty pastures at (4,4) and (0,0),
  worker at (4,4): emits WEST, not immediate PLACE.

The local-job preference cannot choose a nearby job that was excluded from the
candidate list. Repeated requests would need eight movement turns before the
far-corner operation, versus an immediate local operation. These examples prove
wasted travel exists; they do not estimate how much total return it costs.

Assignment is worker-ordered greedy, with priority * 32 + Manhattan distance
(public assignment uses priority * 64). There is no global minimum-cost matching
or persistent worker/job commitment. Mode 2 deliberately replans each turn.
These are limitations, not automatically bugs in every situation.

Ordinary core movement is bounds-only; workers/tiles do not form blocking walls
requiring elaborate collision-aware BFS. Candidate selection, allocation,
resource ownership and task persistence are more relevant than maze pathfinding.

## 4. Empty housing cannot be reclaimed through current macros

The core permits DIG on an empty coop/pasture. Mode-2 crop reclamation counts
only EMPTY/WEED; its DIG jobs cover weeds, not unused buildings. There is no
separate reclaim-building macro. Synthetic fixture confirms no macro emits the
legal local structure-DIG operation, while the primitive succeeds.

Animal intent computes `build_needed = quantity - empty_room` before its later
purchase-affordability cap. A large requested batch can build housing for stock
the current budget cannot buy. In these traces, mean empty housing near turn24
is 6.75 for the parent and 4.0 for the candidate. We fixed reward credit for empty
housing earlier; we did **not** add a route to repurpose those buildings.

Related capability gap: mode 2 can buy/collect/sell fertilizer but its job
generators never emit FERTILIZE. A live-crop/carried-fertilizer fixture confirms
all macros omit it while the primitive succeeds. It is unknown whether adding
fertilization improves profit enough to justify the labor.

## 5. Observation/encoder review

No new encoder matrix/normalization wiring error was identified in this review.
The remote encoder matches local byte-for-byte: a learned 1,280 -> hidden-size
linear projection, then nonlinear MinGRU layers, then the policy/value decoder.
Input bytes are divided by 255 once. The encoder backward weight product has
the same implementation as the generic encoder. This is inspection, not a new
full numerical-gradient audit.

The observation already has farm/product/quadrant summaries, not a raw full
tile map. It has worker positions, local tile state and nearest-target offsets.
The policy selects a planting quadrant, not an arbitrary tile; the animal
placement routine does not accept that spatial target. A better neural encoder
cannot directly undo an executor-chosen position absent from its action space.

Known information limits remain:

- Cash has roughly $392 bins and saturates at $100k: $0 and $390 encode equally;
  $120k and $200k encode equally. Affordability masks and cash-margin features
  supply additional information, so the whole observation is not identical.
- Current market inventory/spot prices are exposed, but macro sale scores still
  multiply stock by spot price instead of using the nonlinear liquidation curve.
- Shared product/tile branches could improve sample efficiency; they are not
  demonstrated to be the primary bottleneck here.

## 6. CPU behavior-probe state mismatch discovered

Existing `kg_experiment_observation` constructs a fresh zeroed Env each call;
`kg_experiment_action` also discards its Env after decoding. Therefore observation
bytes 1243–1245 (previous intent/quantity/target) stay zero in that diagnostic,
unlike native training. Neural recurrent state was retained; this is a separate
adapter-context loss.

The standalone audit probe preserves Env macro state. It detects differences
at exactly those three bytes when compared to the old helper on the same states.
This qualifies prior CPU behavior timelines. **Native GPU PSRO ranking does not
call that helper, so this does not invalidate its parent-beats-candidate result.**
Production helper was not changed during diagnosis.

## Recommended next bounded work

1. Correct the CPU diagnostic state lifecycle and add observation-sequence parity.
2. Test a versioned executor change that consistently completes already-purchased
   livestock on DIVERSIFY as on other structured intents. Preserve old semantics
   for legacy league/checkpoint evaluation and exports.
3. Choose placement/planting candidates with worker distance before enforcing
   batch count; preserve local work, resource feasibility and worker uniqueness.
4. Design explicit reclamation and eventually retire/replace the whole-strategy
   fallback in a separate training lineage. Avoid silently taking control of
   additional purchases/crop choices away from PPO.
5. Compare fixed-policy behavior and matched training runs with unchanged rewards
   before trying encoder branches. No promised $100k outcome.

## Reproducibility

Remote artifact directory contains `probe.c`, `probe.so`, `trace.py`,
`fixture_results.txt`, and eight `parent_s*_p*.json` / `candidate_s*_p*.json` files.
Each trace records requests, legal macros, market orders, per-worker inventory,
primitive commands and effects, per-turn stock/cash/farm counts, and daily snapshots.

Source hashes at inspection:

- kaggriculture.h: `8036351c73283ac5c652d8f779be0ca40c969e0db37d205a37c9537b3ed1f4d7`
- kaggriculture_core.c: `50804bb4bdd6c91d7d4c95b9626dace77b0604023477479d33aa738063c9a6e1`
- kaggriculture_encoder.cu: `85e34e29c70f9a5e66bdcad01db41b6a3c4109e471c7c6070dcc8d89e512e588`

Build the standalone diagnostic from repo root:

```bash
gcc -O2 -shared -fPIC -I. -Isrc -Ivendor \
  -Iraylib-5.5_linux_amd64/include -Iocean/kaggriculture \
  logs/kaggriculture/executor_audit_20260909/probe.c \
  raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -lpthread \
  -o logs/kaggriculture/executor_audit_20260909/probe.so
OPENBLAS_NUM_THREADS=1 /venv/main/bin/python \
  logs/kaggriculture/executor_audit_20260909/trace.py
```

Rerunning overwrites only this diagnostic's JSON reports. Use a separate output
directory when comparing changed implementations.
