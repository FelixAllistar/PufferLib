# Isolated multi-intent / BC pilot — 2026-09-20

September 21 follow-up: see [NEXT_EXPERIMENTS_20260921.md](NEXT_EXPERIMENTS_20260921.md)
for the separately staged remote workspace, run-0-matched reward profile,
current derived dataset and GPU qualification. The status below describes the
original September 20 local pilot and is retained for provenance.

This is an experimental checkout, **not a change to the running sweep**:
`/home/felix/puffertank/pufferlib-multi-intent`.
The original checkout is `/home/felix/puffertank/pufferlib`. No remote files,
checkpoints, reward settings or running jobs were changed for this work, and
no GPU job was launched. Raylib is a read-only dependency symlink; builds and
datasets have independent output paths. The worktree starts at `39f7993c5`
and includes the earlier Kaggriculture BC/core-parity work, not the unrelated
Retro/Webnav edits in the original checkout.

## Controller

Explicit opt-in: `macro_mode=2`, `macro_executor_version=2`, decision interval 1.
The old 2/1 implementation is retained; modes 1/3 were not deleted or renamed.
The main default/sweep config was not switched to this experiment.

| Policy heads | Meaning in 2/2 |
| --- | --- |
| 0–14 | Five `(intent, batch, region)` request slots; STOP terminates the production queue |
| Intent | Plant crops, build coop/pasture, clear, fertilize, harvest any/specific crop, deliver specific/all carried goods |
| Batch | Exact 1–44 worker assignments this turn, including workers routing toward work |
| Region | Automatic, NW, NE, SW, SE |
| 15 | 0: automatic feed; 1: explicit purchases; 2: also reserve shed wheat from automatic pickup |
| 16 | 0: automatic ripe-crop harvest; 1: explicit crop harvest only (animal chores still automatic) |
| 17–46 | Ten ordered `(continue, command, exact quantity)` market slots |

One shared allocator handles production and routine feeding, watering, care,
ripe harvest, fertilizer collection, and livestock pickup/placement. Workers,
target tiles, seeds and compatible livestock housing are reserved across
requests. No hidden opponent orders are used. Investment, herd composition,
hiring, expansion and sales belong to the policy; production requests do not
silently buy seeds/animals. Automatic feed can append an affordable shortfall
order after explicit orders when a queue slot remains. It never displaces a
policy order; the policy can opt out. A full queue therefore defers auto-feed.

Explicit requests reserve workers before automatic chores. Fertilization gets
priority over watering/clearing by scarce local fertilizer carriers, including
renewal of an active fertilizer effect when it extends duration. Lack of feed
does not block feasible care, animal harvest or fertilizer collection. Delivery
reserves shared shed capacity and falls back to partial item PLACE when DROP
would discard excess cargo. Policy-selected wheat reservation prevents feed
pickup from invalidating a planned sale; market prefix masks preview the same
worker transfers. None of this changes the old 2/1 allocator.

Policy ABI **5** has 1,978 action logits and exact market quantities 1–100.
It rejects the earlier v4 prototype despite its equal output size: the new
intent and control meanings must not silently reinterpret saved weights.
Observation layout remains float-v3/1,424 features; v3's price-quote bins were
preserved independently of the new action quantities. Task feasibility/history
is controller-specific. Only the first request is in the legacy history fields;
requests are not persistent hidden plans. The recurrent policy carries context.
Old policy-v3 weights and datasets are rejected, not reinterpreted. In particular,
do not load an existing v3 2/1 model or opponent bank into this experimental
binary; keep using the original checkout for the live baseline.

## Prepared data

Profile: `bc_2_2_pilot.ini`, with the same frozen reward, discount and H512/L3
settings as `bc_2_1_pilot.ini`. At the user's request, the new profile now sets
`land_buy_min_days=0`: no artificial land-buy delay. The old 2/1 profile and live
sweep retain their existing settings. Affordability and actual game rules still
apply. This snapshot is not a claim that a later live sweep has these settings.

Current artifact:
`/home/felix/puffertank/elite_replays/bc_multi_2026-09-20/entity_2_2_pilot_policy5_r3.bc`

Sibling `.json` and `.intents.jsonl.gz` files contain contracts, split, source
hashes, coverage and every original primitive action. Earlier policy4 artifacts
are superseded and fail the current policy/source checks. Nothing was overwritten.

- 16 previously parity-checked Majkel1337 games; 13 train / 3 episode-held-out.
- 720 observations per game, 11,504 nonterminal expert-return targets.
- 9,306 train / 2,153 held-out rows have at least one actor label.
- Source fingerprint `7b7c37cbb92d935e`; reward/controller fingerprint
  `4c1e6cc8e69d966b`; native gamma `0.9993382692337036`.
- 70,686,808 bytes. Cached-tape rebuilding took 17.5 seconds, including local
  compile contention. No large archives were re-parsed
  and no additional replay downloads were performed.
- Same display name does not establish a single immutable submission revision;
  that provenance check remains open before scaling the teacher corpus.

The projector retains simultaneous observed production and exact ordered
trades. It does not invent destinations for moving workers. Unsupported or
masked requests suppress dependent labels, never silently become a different
action. The trainer now accepts market-only/partially labeled rows rather than
requiring the first production head to have a label. Prefix masks come from the
same native controller as PPO. Old 2/1 still consumes only its own projector.
Current-state native action previews distinguish useful board changes from
failed planting and redundant fertilizer consumption. These no-op/wasteful
commands stay in the raw sidecar but aren't mandatory strategic labels. Crop
harvest timing and shed deposits now have explicit labels. Overflowing request
sets are compacted only when native previews verify equal operation/crop/region
counts; otherwise they remain visibly unsupported, never silently truncated.

Coverage on these 16 games (diagnostics, **not model performance**):

| Measurement | Count |
| --- | --- |
| Nonempty market queues fully labeled, preserving order/quantity | 6,572 / 6,806 (96.6%) |
| Native preview matches RAW production operation/type counts | 4,676 / 4,792 (97.6%; previously 66.6%) |
| Useful production operation/crop/REGION counts match | 4,785 / 4,792 (99.85%) |
| Useful strategy counts including crop harvest/delivery match by region | 7,797 / 7,852 (99.30%) |
| Crop-harvest operation/crop/region counts match | 3,870 / 3,921 (98.70%) |
| Complete primitive-action match, including routing/chores/market | 116 / 11,504 |
| Turns containing more than one observed production family | 1,460 |
| Turns with at least one unknown movement goal | 10,980 |

Do not confuse counts with an identical strategy or a learned policy's score.
Different workers/tiles and delivery quantities can still differ. Movement
destinations remain unknown, with no per-worker override or inferred future
plan. The five-slot/44-assignment bounds still overflow on 7 teacher turns;
42 others were compacted with native effect checks. All 234 remaining first
market-prefix conflicts on this teacher were zero-fill (180) or partial-fill
(54) requested orders, not fully filled orders. Later suffixes remain unlabeled;
this is NOT a claim that every realized trade is supervised.

Broader audit: both seats of all 16 cached games (23,008 turns, six display
names), without mixing their data into the teacher dataset. Useful production
counts matched 9,433/9,452 regional signatures, and extended strategic counts
matched 15,068/15,202. There are still 19 request overflows, 26 fully filled first
market conflicts among the other teachers, and unsupported/partial/no-op order
quantities. Thus the sample supports high strategy coverage, not universal
expert equivalence or verified leaderboard rank. Audit artifacts:
`audit_before_r3.json`, `audit_priority_r3.json`, and
`audit_all_policy5_land0_r3.json` under the data directory above. Original replay
trajectories and terminal cash were preserved in every audit.

## Critic

`bc.value_coef=0` is actor-only BC; a positive value enables joint expert-return
regression with the same recurrent encoder. Returns use the production reward
code and gamma, not final cash pasted onto each state. The value-loss scale uses
training-set variance only. This pretrains `V(state, history)` under expert
continuation, **not** a DQN `Q(state, action)` head. PPO must recalibrate it as the
policy changes. Reset-start trajectories are not included in this pilot; retain
the existing reset machinery for later online experiments. Changing rewards,
gamma, rules or controller requires regenerating the inexpensive derived data.

## Checks / next gates

Passed: 35 Python tests, including compiled CPU-only BC preflight, old-policy /
reward / gamma / split / truncated-file rejection, projector edge cases and a
719-step comparison against the original policy-v3 2/1 library. That comparison
matched observations and rewards exactly and matched decoded actions across
varying legacy requests. Native adapter, reward, prefix-mask, observation /
checkpoint, land-delay, shed-placement and episode-metric suites also passed.
New 2/2 tests cover concurrent crops/building/chores, shared seeds/housing,
market cash reuse, quantity-less HIRE accounting, exact quantities, feed opt-out,
fertilizer-before-water, specialized worker priority, crop harvest/wait,
safe multi-worker deposits, wheat reservation, and a complete masked
random-policy game. Repeat land purchases in the new zero-delay profile and
read-only native work/fill diagnostics are covered explicitly.

The BC trainer cross-compiles for `sm_86`. **No GPU forward/backward test,
trained BC model, game-score result or GPU throughput claim exists yet.**
An optional full CUDA-environment test build was stopped in assembly after
it became stale during the final shared-housing fix. It is not counted as a
passing check; a fresh full environment build remains pending. The current
BC trainer and CPU bridge were rebuilt after the current changes and their
preflight fingerprints match the policy5 r3 dataset, including joint value loss.
The old Python submission exporter does not support this new policy/controller;
do not try to submit it through the old 1,058-logit runtime.

Before spending a full training budget: run GPU loss/gradient and CPU/GPU executor parity
smokes; measure steps/sec and memory; then compare 2/1, fresh 2/2, BC 2/2, and
BC+value 2/2 with matched rewards, encoder, seeds and reset settings. Export
parity is a separate gate. None of those gates authorizes interrupting a sweep.

CPU-only verification, from the isolated checkout:

```bash
CUDA_VISIBLE_DEVICES= ocean/kaggriculture/build/multi_intent/kag_bc \
  bc.mode=train bc.profile=ocean/kaggriculture/bc_2_2_pilot.ini \
  bc.data=/home/felix/puffertank/elite_replays/bc_multi_2026-09-20/entity_2_2_pilot_policy5_r3.bc \
  bc.verify_only=1 bc.value_coef=0.1
```

Rebuild only into `BUILD=build/multi_intent`. Use `uv run --no-project --python
/home/felix/puffertank/.venv-kag-bc-audit/bin/python python ...` for Python tools.
Keep outputs versioned and exclusive. Do not sync this worktree over the live
trainer or use its default sweep configuration to launch the experiment.
