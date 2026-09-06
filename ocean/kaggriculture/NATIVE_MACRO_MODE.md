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

As of 2026-09-05, structured mode also operates retained assets across macro
switches: feed/care/watering, animal and ongoing-crop harvesting, fertilizer
collection, and pickup/placement of already-owned livestock into compatible
empty housing. BUY_ANIMAL followed by HOLD therefore completes placement and
collects production without further animal/harvest choices. Inventory drops at
the game's normal end-of-day boundary; collecting products does not sell them.
One-shot crops are automatically harvested at maximum-yield age (or during the
last day). Explicit HARVEST can request earlier legal collection while still
protecting maintenance. An explicit species-production action retains its
batch limit; background placement resumes on other actions. This does not
persist an unpurchased investment batch or automatically build missing housing,
and does not add an abandon/cancel action. Mode 3 remains fully task-controlled.

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

## Mode 3: PPO-owned task controller

`macro_mode = 3` removes the remaining generic-farm strategy from the
executor. The 17 existing unit heads become an ordered set of task requests;
the ten existing conditional market slots remain policy-owned. Lower unit
head numbers have conflict priority, but no head is tied to a particular
worker. For each request, native code chooses the closest unused worker and
closest compatible target and emits one legal movement/work command.

The 44 task values are:

* `IDLE`, water, fertilize, feed, care, crop harvest, animal harvest,
  fertilizer collection, and shed drop;
* clear/abandon in each of four quadrants;
* each of five crops in each of four quadrants;
* coop and pasture construction in each quadrant; and
* add goose, cow, or sheep to compatible capacity.

Repeated task values across heads express quantity. PPO also decides every
buy, sale, hire, and land order—including item and quantity—through the market
queue. The executor never chooses a crop, species, investment, sale,
liquidation, or maintenance policy. It may route a worker to the shed and
pick up a required seed/product/animal already owned; it never purchases that
prerequisite. `IDLE` is genuinely idle.

This gives PPO ownership of the strategic and spatial decisions while keeping
worker identity, assignment, and grid routing as deterministic mechanics. It
also permits explicit abandonment through the quadrant `CLEAR` tasks. The
fixed ABI directly schedules the farmer plus sixteen hands; farms with more
workers require a future cohort/assignment extension.

## Observation input

The base observation remains byte-for-byte 1,280 bytes. Modes 1 and 2 fill the
previously unused tail after the reset-source byte with 44 bounded,
public-state candidate-score bytes plus the active intent and remaining sticky
ticks. The high bit of each candidate byte is a legality flag; the lower seven
bits are a coarse native economic estimate. These are **features**, not reward
terms, and do not add crop, animal, land, or selling reward.

`macro_score_scale` controls the estimate quantization (default `10000`). The
scores currently use visible cash, prices, costs, production timing, inventory,
remaining time, and physical farm capacity. They do not load a
Ridge/LightGBM file yet and they never read opponent private inventory.

Mode 3 does **not** expose those economic estimates. Its same 44 tail bytes
contain only public mechanical task capacity: the high bit is task legality
and the low seven bits are the currently visible compatible-job count, capped
at 127. The final four tail bytes are zero. A task controller can therefore
use any reward/potential ablation without silently inheriting the handwritten
candidate-value formula.

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

Use mode 3 for the PPO-owned task controller. When training it against the
existing mode-2 league, keep each frozen policy on its original semantics:

```ini
[env]
macro_mode = 3
frozen_macro_mode = 2
macro_decision_interval = 1
```

`frozen_macro_mode = -1` makes every policy inherit the learner mode. Policy
ID zero is the live learner; nonzero frozen-bank policies use
`frozen_macro_mode`. Observation tails, legality masks, and action decoding
are selected independently for the two seats in one native game.

With different learner and frozen modes, set `selfplay.opponent_pool_prob=1`
and supply an external league containing only the frozen mode's models.
Rolling task snapshots cannot enter mode-2 banks. Live mirror games still
use the task mode on both seats. End-of-training evaluation swaps the modes
along with the models for its reverse-seat match.

The task dataset and BC/PPO launchers are:

```bash
./ocean/kaggriculture/build_task_bc_dataset.sh /path/to/tasks.bc /path/to/replays.zip
./ocean/kaggriculture/train_task_controller.sh /path/to/tasks.bc task_run_v1 saved/kaggriculture_league_macro_256x3_v1/league.ini
```

The second command defaults to 256x3, weights the first 300 BC turns, balances
task classes, then trains with LR 0.0007 without annealing. It inherits the
current reward configuration. `KAG_TASK_BC_ONLY=1` stops after BC;
`KAG_TASK_BC_EPOCHS=0 KAG_TASK_ANCHOR=/path/to/task.bin` reuses an existing
task checkpoint. Dataset version and controller mode must match; a mode-2
BC dataset cannot train mode-3 task labels.

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

The native tests cover masks, task/score tails, legality fallback, sticky
behavior, task execution, mixed task-learner/structured-opponent dispatch,
and exact submission parity:

```bash
make -C ocean/kaggriculture adapter
make -C ocean/kaggriculture cuda-adapter
PYTHONPATH=ocean/kaggriculture:ocean/kaggriculture/submission \
  python -m unittest ocean/kaggriculture/tests/test_native_task_parity.py
```

Replay BC data for mode 3 is generated with:

```bash
python ocean/kaggriculture/import_elite_replays.py REPLAY.zip \
  --output task.bc --macro-mode tasks --task-lookahead 32 \
  --exact-version 1.32.7
```

Movement and prerequisite pickups are traced forward only to their actual
demonstrated work task. Independent task requests are packed toward head zero;
the demonstrated conditional market queue is retained. The importer reports
unresolved and capacity-clipped labels rather than inventing a macro.

The macro path is compiled into the GPU environment as well; it does not
change the normal primitive path when the switch is zero. Mode-2 and mode-3
checkpoints must be packaged with `package_native_macro_model.sh`; set
`KAG_NATIVE_MACRO_MODE=3` for task models. The export includes the matching
tail, mask, decoder, and portable deterministic executor. Using
`package_model.sh` would incorrectly interpret the same logits as primitive
unit commands.
