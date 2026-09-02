# Counterfactual strategic planning goal

## Objective

Build a policy-improvement loop for Kaggriculture without making the current
primitive PPO policy rediscover every economic mechanic from a sparse terminal
reward:

```text
verified native KGState
  -> feasible macro candidates
  -> exact native-simulator branches
  -> actual cash/advantage labels
  -> learned candidate scorer or ranker
  -> search and/or macro PPO
  -> deterministic primitive executor
  -> native Kaggriculture
```

The true optimization target remains final cash (and, when explicitly chosen,
win/margin). Crop, animal, land, and price guesses are inputs or temporary
credit-assignment aids; they are not silently added to the reward objective.

## What “exact simulator” means

The exact simulator is the parity-tested native rule core in
`kaggriculture_core.c`, the same transition implementation used by the native
trainer/evaluator. It includes market inventory and prices, shop demand,
production, maintenance, legality, randomness, and both players' actions. It
is not a learned price or outcome approximation.

Existing live evaluation executes only the selected policy action:

```text
state -> policy action -> kg_step -> next state
```

This project adds offline hypothetical branching:

```text
one restored state
  ├─ HOLD
  ├─ SELL MILK 10
  ├─ BUY LAND
  └─ BUY_ANIMAL COW 3
```

Every branch starts from the same serialized state, so candidate/baseline
comparisons use common simulator randomness. Offline branches have no
competition wall-clock deadline; only total dataset-generation cost matters.
Optional runtime MPC must be bounded to a few strategic decision points and
must fit the per-turn deadline. It is not part of C0.

## C0 implementation (safe vertical slice)

`macro_actions.py` defines a stable candidate/data interface and an
observation-safety boundary. The original C0 vertical slice restored a
verified `.kgb` bank record, applied one directly representable
market/investment candidate, continued a candidate and same-state baseline,
and wrote a TSV. The default catalog now also appends C1 deterministic
multi-turn plans; pass `--direct-only` to reproduce the original C0 rows.

The first catalog intentionally contains only actions that the current native
core can execute without routing assumptions:

- `HOLD`;
- `SELL` product quantities;
- `BUY_SEED` crop quantities;
- `BUY_ANIMAL` quantities;
- `BUY_PRODUCT` for native buyable products;
- `BUY_LAND`;
- `HIRE`.

Plant/build/maintenance/routing macros are represented as explicit sequences,
never as `PASS`; infeasible sequences are omitted. This avoids training on
false no-op labels.

The target is explicit:

```text
delta_money(s, a) = candidate_money_after_horizon
                    - baseline_money_after_horizon
delta_money_normalized = delta_money / starting_money
```

For learned-policy continuations, a candidate is applied as a strategic
overlay on that policy's primitive action: explicit farmer commands and the
candidate market queue are replaced, while unspecified hired-hand work is
preserved. `HOLD` therefore has exactly zero delta and means “let the
continuation policy act,” not “inject one PASS turn.” The live episode gate
uses the same merge, so offline labels and deployment have identical action
semantics.

For `--horizon terminal`, both branches run to the genuine episode terminal.
For a numeric horizon, the output is a short-horizon target and is marked
non-terminal unless the state naturally reaches the end.

Continuation modes are deliberately named:

- `pass`: both players pass after the first action; useful only as a smoke test;
- `expert_first`: use the recorded next joint action once, then pass;
- `trace`: use a caller-supplied JSONL action trace at each subsequent turn.

None of these is a reactive opponent policy. The `learned` provider mode
instead loads the selected PPO league and re-acts after every native
transition; pass-only rows must never be described as robust PvP labels.

Example smoke run:

```bash
python3 ocean/kaggriculture/counterfactual_dataset.py \
  --bank /home/felix/puffertank/elite_replays/state_bank/full_1365_each.kgb \
  --output /tmp/kag_counterfactual_smoke.tsv \
  --limit-states 2 --max-candidates 12 --horizon 1 --opponent-mode pass
```

The output has fixed numeric `feature_*` columns for tree models plus JSON
columns for audit/replay. The feature builder may read the selected player's
private shed/seeds, because those are observable to that player, but it never
reads the opponent's `privates` entry. Public opponent farm state, market, and
shops are allowed.

The TSV can be fit immediately with the dependency-light ridge baseline. It
uses a stable episode-level split (so rows from one episode cannot leak into
both partitions) and writes a model plus auditable metadata:

```bash
python3 ocean/kaggriculture/fit_macro_value.py \
  --dataset /tmp/kag_counterfactual_smoke.tsv \
  --output /tmp/kag_macro_value.npz \
  --backend ridge
```

The model predicts `delta_money` relative to the same-state baseline; it is a
candidate scorer, not a policy and not a replacement for native evaluation.
`--backend lightgbm` is optional when LightGBM is installed. A dataset with
only one episode cannot form a held-out episode split and is rejected rather
than reporting a misleading validation score.

Trace JSONL uses one object per transition:

```json
{"episode_id":"93459920","turn":432,
 "actions":[{"farmer":["PASS"],"hands":[],"market":[]},
            {"farmer":["PASS"],"hands":[],"market":[]}]}
```

## C1: learned scorer and PvP branches

Before fitting a model, validate the labels with actual native rollouts. Use
episode/lineage-held-out splits, not random rows. Start with a few thousand
states stratified by early/mid/late turn, product opportunity, production,
maintenance, and inventory.

The first learned model should score one row per `(state, candidate)` and can
use a regression target or a ranking objective grouped by state. Candidate
kind, item, and quantity are features; separate models per crop are not
required. Train separate short/medium/terminal targets if useful.

For PvP labels, replace pass/trace continuations with a sampled opponent league:

```text
candidate action
  -> current policy / old policy / rule bot / specialist opponent
  -> native continuation
  -> final cash, win, margin, and downside
```

The opponent mixture is part of the label definition. A fixed recorded tape is
not a reactive best response and should be used only as a diagnostic.

## C2: search and macro policy

First compare a greedy scorer and short-horizon MPC. Search should evaluate a
small top-K candidate set every day or after an important event, not every
primitive action. A leaf can be scored by a learned value model:

```text
Q(s, a) ~= native rewards over H turns + V_model(state_at_H)
```

Only after this baseline beats or explains the existing champion should we add
macro PPO. Macro PPO receives raw strategic features and candidate scores,
chooses among masked candidates, and learns sequencing/risk/opponent response.
The first native macro executor now exists as an opt-in `macro_mode=1` path;
it reuses the existing 44-way unit head and keeps the primitive ABI sizes
unchanged. The macro path is documented in `NATIVE_MACRO_MODE.md`; the
default `macro_mode=0` primitive path remains the safe fallback until a
macro-trained policy has passed evaluation.

## C1/C2 implementation status (completed)

The remaining phases are now implemented as an offline, testable reference
pipeline.  The implementation is intentionally additive: the live primitive
PPO configuration and its action ABI are untouched.

### C1: executor, reactive branches, and fitted scorer

`macro_executor.py` turns feasible strategic requests into real primitive
sequences.  It uses the native bounds-only movement rules and checks visible
cash, seeds, animal inventory, tile capacity, shed capacity, and remaining
turns.  The supported plans are:

* `PLANT:<crop>:<quantity>` (buy missing seeds, route, plant);
* `BUILD_ANIMAL:<animal>:<quantity>` (buy/pick up, build a coop/pasture,
  place);
* `HARVEST_READY`, `MAINTAIN_DUE`, and `SELL_ALL`.

The plan is auditable JSON in every candidate row.  It is not a hidden
shortcut: the brancher steps each primitive action through the same native
core and rejects a plan that cannot fit the remaining episode.

`kg_rule_action_ex` in `kaggriculture_core.c` is a side-effect-free native
reactive continuation.  It sees the current full state after every transition,
liquidates in a configurable final window, expands when affordable, buys the
best visible seed, and assigns nearest maintenance/harvest/plant/place jobs.
`--opponent-mode rule` uses this function, while `pass`, `expert_first`, and
`trace` remain explicit diagnostic modes.  Rule labels are PvP labels against
an adaptive scripted opponent, not claims about a learned league best
response; a learned PPO provider can implement the same `ActionProvider`
protocol without changing the dataset contract.

`counterfactual_dataset.py` now emits v2 rows with numeric state/candidate
features, plan JSON, first-step and horizon cash, terminal flags, and the
same-state cash advantage.  Run one dataset per target horizon/provider so
the target definition is never ambiguous:

```bash
python3 ocean/kaggriculture/counterfactual_dataset.py \
  --bank /path/states.kgb --output /path/value_24_rule.tsv \
  --horizon 24 --opponent-mode rule --max-candidates 0
python3 ocean/kaggriculture/counterfactual_dataset.py \
  --bank /path/states.kgb --output /path/value_72_rule.tsv \
  --horizon 72 --opponent-mode rule --max-candidates 0
python3 ocean/kaggriculture/counterfactual_dataset.py \
  --bank /path/states.kgb --output /path/value_terminal_rule.tsv \
  --horizon terminal --opponent-mode rule --max-candidates 0
```

For a learned league, use the same command with `--opponent-mode learned`, an
explicit `--league`, and `--policy-backend torch --policy-device cuda` when a
CUDA-capable host is available.  The native policy-view shim writes the
submission observation and legality mask directly from `KGState`; it is
byte-for-byte checked against the Python submission adapter.  Snapshots in a
`.kgb` bank do not contain recurrent hidden state, so learned offline branches
are explicitly *snapshot-local*: the selected league member is reset at each
decision.  This is not silently presented as live recurrent parity.

Large learned runs can be split after deterministic state selection with
`--state-shard-index N --state-shard-count M`.  Each shard writes the same
header/schema and can be merged without loading all rows into memory:

```bash
python3 ocean/kaggriculture/merge_counterfactual.py \
  --output /path/value_24_learned.tsv \
  /path/shard0.tsv /path/shard1.tsv /path/shard2.tsv /path/shard3.tsv
```

`--branch-batch-size` controls the number of independent native branches
whose learned opponent observations are forwarded in one Torch batch.  It
changes only scheduling, not the candidate sequence, native transitions, or
cash labels.

`--branch-workers` can split those candidate batches across CPU workers.  Each
worker owns its native states and provider RNG; outputs are reassembled in
candidate order.  Deterministic labels are therefore byte/reproducibly
identical to the one-worker reference, while stochastic labels receive
independent streams.

`fit_macro_value.py` fits the dependency-light standardized ridge scorer or,
when LightGBM is installed, a boosted regressor/ranker.  `--objective rank`
converts each state's cash advantages into deterministic within-state
relevance labels (mapped to LightGBM's supported 0--30 relevance range) and
preserves the original cash target in metadata.  Both models are
observation-safe and split by complete episode IDs.

### C2: bounded greedy/MPC evaluator and policy boundary

`macro_planner.py` is the reference policy-improvement evaluator.  `greedy`
selects the model's highest candidate.  `mpc` first keeps only the model's
top-K candidates, then exact-rolls each from the identical serialized state
through a bounded native horizon and selects the best realized cash.  The
output records both choices, every exact top-K result, the continuation mode,
and a JSON summary; it is suitable for held-out episode/lineage evaluation.

```bash
python3 ocean/kaggriculture/macro_planner.py \
  --bank /path/states.kgb --model /path/value_24_rule.npz \
  --output /path/planner_mpc.tsv --horizon 24 --search mpc --top-k 4 \
  --opponent-mode rule
```

The same evaluator accepts `--opponent-mode learned --league ...` and the
Torch/native-view options.  In that mode every exact top-K branch continues
against a selected PPO checkpoint, rather than a prerecorded tape.  A
separate live gate, `macro_episode_eval.py`, runs complete 720-turn native
episodes: it compares a primitive PPO baseline and the macro controller
against the same PPO opponent and seed.  Greedy selection is made only at a
sparse `--decision-interval`; `--macro-mode mpc` additionally exact-rolls the
top-K candidates for a bounded lookahead.  This is the PPO comparison command
used for the acceptance gate:

```bash
python3 ocean/kaggriculture/macro_episode_eval.py \
  --macro-model /path/macro_learned_72.npz \
  --league /path/league.ini \
  --baseline-model /path/primitive_ppo.bin \
  --output /path/episode_compare.json --episodes 2 \
  --macro-mode greedy --decision-interval 24 \
  --policy-backend torch --policy-device cuda
```

The JSON records per-opponent money/win deltas, every macro decision, model
backend, and whether evaluation was deterministic or stochastic.  A model is
not called “better” from ridge RMSE alone: the held-out native planner and
full-episode PPO comparison are the required evidence.

The live controller is an overlay rather than an idle replacement: PPO keeps
acting between strategic boundaries and its recurrent state is advanced even
when a macro overrides the current turn. Before applying a non-HOLD proposal,
the gate compares an exact common-random-number native lookahead against the
PPO fallback; proposals that do not improve that lookahead are rejected. This
makes deterministic and stochastic comparisons meaningful and prevents a
model-only extrapolation from silently degrading the underlying PPO policy.

The fitter retains the explicit `HOLD` rows as zero-delta reference examples
while dropping rejected non-HOLD rows.  `HOLD` is marked ineffective by the
native brancher because it is the unchanged baseline, but removing it from a
regressor makes the live safety comparison extrapolate a fictitious baseline.
This distinction is covered by `test_macro_value.py`.

For live recurrent episodes, the proposal branch forks the opponent provider at
the current hidden state and RNG.  Offline replay snapshots do not contain
hidden tensors and remain snapshot-local; the live fork is what makes the
terminal guard an actual same-opponent comparison.  `--guard-horizon 0` keeps
the bounded lookahead behavior.  Setting `--guard-horizon 720` (or the
remaining episode length) performs an exact terminal-cash guard while the
MPC ranking itself can remain bounded:

```bash
python3 ocean/kaggriculture/macro_episode_eval.py \
  --macro-model /path/macro_learned_72_lgb.txt \
  --league /path/league.ini --baseline-model /path/primitive_ppo.bin \
  --output /path/compare.json --episodes 2 --macro-mode mpc \
  --decision-interval 24 --lookahead 72 --guard-horizon 720 \
  --policy-backend torch --policy-device cuda
```

### Completed learned-league run

The reproducible artifacts for the current C1/C2 run are in
`/home/felix/puffertank/macro_pilot_learned_final/`.  They were generated from
256 stratified state-bank records (512 player views), all six enabled
512x2 PPO members in
`/home/felix/puffertank/vast_backup/saved/kaggriculture_league_512x2_elite_v5/league.ini`,
and the parity-tested native core.  The 24-turn, 72-turn, and terminal TSVs
each contain 16,733 candidate rows; 15,844 are effective after native
execution, and all 512 `HOLD` rows are exact zero-delta references.  Each
horizon has both standardized Ridge (`*_ridge.npz`) and LightGBM
(`*_lgb.txt` plus metadata) scorers.

Held-out (validation episodes, 32 state records/64 player views) with the
corrected 72-turn LightGBM model:

| selector | mean baseline cash | mean selected cash delta |
| --- | ---: | ---: |
| greedy | 24,059.9 | +211.3 |
| exact top-4 MPC | 24,059.9 | +513.2 |

The full native episode panel used `animal_alt_100m.bin` as the fixed primitive
PPO baseline and each of the six league members as the reactive opponent (two
720-turn episodes per opponent).  The recommended Ridge scorer with the
720-turn terminal guard gained **+$862/episode deterministic** and
**+$974/episode stochastic** across the six-opponent panel.  The deterministic
panel had no winner flips; two stochastic episodes changed the winner despite
the cash improvement, so this small panel is not a win-rate claim.  The
LightGBM scorer gained +$442 deterministic and +$26 stochastic under the same
guard.  Unguarded greedy proposals were negative
(−$2,567 in the stochastic panel), and an unguarded terminal-model diagnostic
lost much more; those failures are why the terminal guard is part of the
recommended runtime command rather than silently treating a short-horizon
proposal as safe.  These are small sanity panels, not a claim of statistically
significant leaderboard improvement, but they demonstrate the intended
progression: exact native labels, learned PvP scoring, recurrent PPO fallback,
and a safety gate that can reject harmful interventions.

`macro_policy.py` defines the versioned JSON boundary for a future macro PPO
policy: public strategic features, candidate descriptions, optional scorer
predictions, and a validated candidate index.  Invalid or out-of-range
decisions are rejected; the selected candidate's deterministic primitive plan
is then the only thing an executor needs.  No 1,058-head policy checkpoint can
be loaded through this boundary accidentally.

The exact simulator remains `kaggriculture_core.c`; it is the same native
transition code used by the parity and CUDA tests, not a learned market model.
Offline branching has no per-turn competition deadline.  A future runtime
integration must call the scorer/search only at sparse strategic decision
points and stay within the normal turn budget.

### Reproducible gates

```bash
make -C ocean/kaggriculture lib
make -C ocean/kaggriculture counterfactual-test
make -C ocean/kaggriculture cuda-core
make -C ocean/kaggriculture adapter
```

The counterfactual test target covers public-feature privacy, direct and
multi-turn execution, reactive providers, model loading, planner output, and
decision validation.  Native CUDA/core and adapter gates remain independent,
so these additions cannot silently alter live PPO behavior.

Deterministic code may execute routing, legal order construction, worker
assignment, and mandatory terminal liquidation. It must not automatically sell
throughout the season: deciding hold versus sell remains strategic.

## Acceptance gates

1. State-bank ABI/version/checksum validation passes.
2. Candidate and baseline branches use identical starting bytes and common
   randomness.
3. Opponent-private fields cannot change feature columns.
4. `HOLD` matches the baseline exactly under pass continuation.
5. A known legal sell/buy action changes the native state and cash as expected.
6. Terminal labels are actual final money, not potential/progress reward.
7. Held-out episode-level rollouts improve against at least one opponent panel
   before any live PPO integration.

No training config, reward code, checkpoint, or submission package is changed
by C0.
