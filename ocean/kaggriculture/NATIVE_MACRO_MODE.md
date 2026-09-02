# Native Kaggriculture macro mode

Macro mode is an opt-in strategic-action decoder. It is **not** a smaller
policy architecture.

## What changes

With the default `macro_mode = 0`, the policy has the normal 47-head ABI:

* 17 unit heads (farmer plus direct hands), each with 44 primitive commands;
* ten conditional market slots; and
* the existing 1,280-byte observation and mask layouts.

With `macro_mode = 1`, the same policy and the same MinGRU/encoder sizes are
still used. The first 44-way unit head is reinterpreted as a strategic macro
selector. The native executor then expands that selector into an ordinary
`KGAction` using the existing rule/planner code. The other unit heads are
masked to `PASS` and the market slots to `STOP`, so they cannot fight the
macro executor.

With `macro_mode = 2`, heads 1 and 2 are parameter heads in addition to the
macro selector:

* head 0: macro intent;
* head 1: quantity bins `1, 2, 4, 8, 12, 20, 32, 64`;
* head 2: target `AUTO, NW, NE, SW, SE`.

The quantity is a requested batch/target for the current strategic decision
and is re-evaluated at each decision boundary. It is clipped by the native
executor to legal cash, stock, capacity, and board limits. `PLANT` uses it to
bound new planting and seed acquisition; `BUY_SEED`, `BUY_ANIMAL`, `SELL`,
`BUY_PRODUCT`, and `HIRE` use it as the requested amount. The target quadrant
currently constrains planting; other operations use the game's native
sequential land rules. Parameter heads are masked to their small legal ranges
only in mode 2, so the checkpoint and action-mask ABI remains unchanged.

In mode 2 the selected macro owns strategic market actions. The executor keeps
only feed purchases required to prevent existing animals from starving and
the selected macro's orders; it does not silently add unrelated land, growth,
sales, or hires. Worker assignment and movement remain deterministic.
`DIVERSIFY` is the explicit exception: selecting it delegates one turn to the
portable generic farm plan, including its crop mix, sales, scheduled land and
labor, plus a first-pasture bridge. Mandatory feed is ordered before optional
investment so sequential market execution cannot starve existing animals.

The current macro IDs cover hold, crop/animal production, land, product
selling, seed/animal purchases, hiring, harvest, maintenance, diversification,
inputs, and cash-out. `macro_decision_interval` makes a legacy mode-1 intent
sticky for that many native turns. Structured mode 2 always decides every
turn: its quantity is a one-decision batch, and replaying that full batch on
sticky turns would silently multiply purchases, hires, planting, and sales.

## Observation input

The base observation remains byte-for-byte 1,280 bytes. Macro mode fills the
previously unused tail after the reset-source byte with 44 bounded,
public-state candidate-score bytes plus the active intent and remaining sticky
ticks. The high bit of each candidate byte is a legality flag; the lower seven
bits are a coarse native economic estimate. These are **features**, not reward
terms, and do not add crop, animal, land, or selling reward.

`macro_score_scale` controls the estimate quantization (default `10000`). The
scores currently use visible cash, prices, costs, production timing, inventory,
remaining time, and physical farm capacity. They do not load a
Ridge/LightGBM file yet and they never read opponent private inventory.

The native estimate also enforces a few accounting constraints that are true
independently of strategy:

* seed purchases are clipped to reclaimable soil minus already-held seeds;
* animal purchases are clipped to compatible empty structures minus unplaced
  animals already owned;
* hired hands are valued only for the remainder of the current day because
  hires expire overnight, and purchase quantity is clipped to visible work;
* the final two days disable new seed, animal, land, fertilizer, and hire
  investment while preserving maintenance, harvesting, feed, and sales; and
* land is valued as several marginal production slots only when the current
  farm is crowded or has a seed backlog, instead of as one crop slot.

These constraints prevent impossible stockpiles and obvious end-game
reinvestment. They remain coarse public-state features: PPO still chooses the
macro and its requested quantity.

## Configuration

```ini
[env]
macro_mode = 1
macro_decision_interval = 1
macro_score_scale = 10000
```

Use `macro_mode = 2` for the parameterized training experiment:

```ini
[env]
macro_mode = 2
macro_decision_interval = 1
macro_score_scale = 10000
```

Keep `macro_mode = 0` for existing primitive policies. Although the tensor
sizes are unchanged and a checkpoint can be read, an old primitive checkpoint
does not have macro-trained action semantics: its first-head outputs are now
interpreted as macro IDs and all other heads are ignored. Train/evaluate a
macro policy with the switch enabled from the start (or treat a primitive
checkpoint only as a cautious parameter warm start).

The native adapter smoke test covers the mask, score-tail, legality fallback,
and sticky-interval behavior:

```bash
make -C ocean/kaggriculture adapter
```

The macro path is compiled into the GPU environment as well; it does not
change the normal primitive path when the switch is zero. Mode-2 checkpoints
must be packaged with ``package_native_macro_model.sh``. That export includes
the strategic observation tail, structured mask, quantity/target decoder, and
portable deterministic executor; using ``package_model.sh`` would incorrectly
interpret the same logits as primitive unit commands.
