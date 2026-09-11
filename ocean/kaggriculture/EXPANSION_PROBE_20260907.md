# Remote growth-only diagnostic and observation review — 2026-09-07

Follow-up: the ownership bug below now has a versioned source fix. See
[the fix/rebuild notes](OBSERVATION_OWNERSHIP_FIX_20260907.md). The next remote
run ID is `expansion4_obs1_20260907`; historical probe settings below describe
the preceding config-only change.

## Scope and active configuration

The file inspected and changed is **Vast's**
`/workspace/PufferLib/config/kaggriculture.ini`, originally run ID
`postexpansiontest`. The local training INI is untouched. No trainer, sweep,
submission, league promotion, or production rebuild was started.

New run ID: `expansion4_peak_probe_20260907`. This is a fresh-policy diagnostic,
not a continuation or an attempt to maximize competitive performance.

| Setting | Before | Diagnostic |
| --- | --- | --- |
| Legacy accounting potential | 1 | 0 |
| Fitted progress potential | 1 | 0 |
| Progress terminal cash | 2 | 0 |
| Terminal win | 1 | 0 |
| Cash shaping / legacy terminal cash | 0 / 0 | unchanged |
| Expansion scale | 100 | 10 |
| Expansion deadline | 360 | 672 |
| Land target, TOTAL plots | 3 | 4 |
| Peak live plants target | 50 | 60 |
| Peak live animals target | 12 | 12 |
| Entropy coefficient | 0.06 | 0.0006 |
| Checkpoint interval, epochs | 192 | 24 |

Inactive progress multipliers are also zeroed for clarity: crop 30 to 0,
animal 30 to 0, product 1 to 0, land 4 to 0, liquidation days 1 to 0.
The fitted per-product constants are preserved but generate no progress reward
while the system is disabled. Maintenance reward is zero; curriculum is off.

Unchanged: LR **0.004**, no LR annealing, 4096 agents, horizon 256, minibatch
2048, 256x3 MinGRU, gamma 0.99970, lambda 0.9899, macro mode 2 with its current
automatic farming, normal 720-turn root episodes, reset probability zero,
no warm start/EMAG, and the existing opponent mixture. The user's 2B training
budget is unchanged. Checkpoints now occur every 25,165,824 steps instead of
201,326,592, allowing earlier behavior to be retained.

Restoring entropy is the one optimizer change: 0.0006 was the successful
autofarm run's value, while 0.06 was 100 times larger. This is deliberately a
best-effort simplified diagnostic, not a one-variable reward ablation. Merely
reducing expansion scale is not expected to solve a strategic plateau; the
important changes are removing the competing objectives and extending the
reward window. A target of three did **not** explain failure to reach three.

## What the reward actually asks for

For episode-high counts, define:

```
L = min((plots - 1) / 3, 1)
P = min(live_plants_peak / 60, 1)
A = min(live_animals_peak / 12, 1)
G = L + P + A + 2 * min(L, P, A)
reward = 10 * (G_after - G_before), through turn 672
```

This is the only active reward, not an economic potential. From a normal empty
root the maximum undiscounted reward is 50. Land alone contributes at most 10;
balanced crop/animal growth unlocks additional credit. Replanting after a loss
does not pay again unless the old live-count record is exceeded.

Important limitations of these **existing** knobs:

- The three peaks need not occur simultaneously. This does not reward keeping
  a full farm alive, nor punish its later collapse.
- It does not reward crop/species variety: 12 geese count the same as 12 cows.
- Financing expansion still requires productive operation and selling; there
  is no separate cash/sale reward. Success at this prerequisite is not assured.
- Four plots is the ultimate target; **three is already meaningful progress**.
  Dashboard `land_purchases=2` means three total plots; 3 means all four.
- Money/score/win are not this run's objective. Check plots, productive extra
  tiles, live counts, neglect, and trajectories rather than promoting by money.
- Native economic observation scores and executor constraints remain enabled.
  Turning reward multipliers off does not turn those features off.

No claim is made that this unrun configuration will reach its targets. A failure
would not, by itself, prove that the encoder or executor cannot support growth.

## Concrete observation finding: farm ownership ambiguity

`kag_write_observation_with_summaries` writes cash, inventory, and workers from
the requested player's perspective, but writes farm summaries as
`summaries[0]`, then `summaries[1]` for **both** seats. There is no explicit seat
identity in that observation.

The diagnostic in `tests/expansion_observation_probe.c` reproduces a collision:
equal money/worker positions/private stock, a wheat plant on player 0's farm,
and a strawberry plant at the same location on player 1's farm. All **1280
bytes are identical between the two seats**, although their own crops differ.
This was reproduced against the deployed Vast adapter header, not inferred
from a dashboard. It creates an unnecessary requirement to infer ownership
from action/history and can matter especially for resets and opposite-seat
generalization. It does not establish the cause of all previous plateaus.

This is a higher-priority representation fix than speculative extra layers.
The fix should make the new observation consistently own-farm/opponent-farm,
with a versioned compatibility path for legacy league policies and submission
parity checks. It was **not** silently applied here: changing byte meaning for
old checkpoints/frozen opponents would confound this run and risk regressions.
Resolve this before spending another large reward-sweep budget.

## Encoder, decoder, and economics review

- The active encoder file is the same on Vast and locally: a bias-free linear
  1280-to-256 projection. The three MinGRU layers contain nonlinear gates and
  hidden activations. The overall policy is **not linear**, and distinct input
  positions already have distinct weights. Flat input does not make it unable
  to distinguish two equal numbers with different field meanings.
- There are no learned product/farm branches. Much of the input is already
  hand-aggregated by quadrant and product, and 816 bytes describe the 17 own
  worker views. It is not a full per-tile observation. A tile encoder cannot
  recover tile geometry discarded by those aggregates.
- A useful future branch encoder would nonlinearly process shared per-product
  features (price, stock, production, demand) and consistently owned farm
  summaries, then fuse them before the same recurrent policy. Splitting a
  linear map into several linear maps and linearly joining them adds no such
  expressiveness. Product embeddings mainly help when paired with shared entity
  processing; they are not a new reward or a new action head.
- The actor and critic share the recurrent features; the current decoder is
  the standard linear map to 1058 logits plus one value output. No decoder
  implementation defect was identified in this targeted source inspection.
  This was not an exhaustive new gradient audit.
- The macro observation still contains native heuristic scores. SELL really
  is shed quantity times current spot; crop/animal scores use rough production
  events and spot prices. No Ridge/LightGBM model is loaded for these bytes.
- The fitted elite production constants belong to the separate reward
  potential, not to a pretrained PPO critic. They are disabled for this probe.
- Nonlinear market liquidation math exists in `kag_product_mark`, but for
  multi-unit quantities it is a **conservative continuous approximation**,
  not the exact rounded per-unit transaction sum. It currently informs asset
  marking rather than exposing a full liquidation curve in the observation.
- Exact single-agent hypothetical sale totals should sum the engine's rounded
  per-unit quotes. They remain conditional on no simultaneous opponent market
  flow, not predictions of future prices or guaranteed execution proceeds.
  The current native curves are pinned to `KG_MARKET_DEFS`; a future feature
  ABI must account for supported runtime parameter overrides too.

The explicit EXPAND macro is not capped at two or three plots. Its legal action
requires funds after feed reservation and enough remaining production time.
The DIVERSIFY delegated plan only schedules expansion up to three total plots;
that restriction does not apply to explicit EXPAND. Hiring also has a
hand-written workload/land cap, and late investment is restricted. These are
real strategic constraints in mode 2, not something a new encoder removes.

## Verification and manual command

The C probe executed the actual macro order path with capital supplied solely
to isolate legality/execution. Three EXPAND orders bought all four plots for
7000 total. This proves that path can execute, **not** that PPO learned it or
can finance it. Reward cap, repeated-count behavior, and deadline checks passed.
The same probe exposed the ownership collision described above. Config parsing
and assertions verify fresh roots, reward gates, and unchanged LR/batch shape.

The prepared manual command, on Vast, is:

```bash
cd /workspace/PufferLib
./puffer train kaggriculture
```

No rebuild is needed for these config changes. The observation bug remains
present; fixing/versioning it is the recommended next code change. The old
Protein ranges remain historical and have positive-only bounds incompatible
with some new zero defaults: use `train`, not `sweep`, for this diagnostic.
