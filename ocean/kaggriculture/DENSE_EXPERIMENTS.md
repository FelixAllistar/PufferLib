# Dense rewards and controller experiments (fresh policy v3)

All six meaningful mode/executor combinations use the entity encoder, dense
rewards, reset-state banks, self-play, and checkpoint leagues. Start fresh: v2
weights are rejected because controller/history feature meanings and masks changed.
The 1,424-float observation size, network layers, and reset-bank file format are unchanged.
Replay reset-state bank import is currently a GPU-environment feature: build with
`bash build.sh kaggriculture --gpu` for those experiments. CPU viewers start from
the normal opening; CPU training checks use `reset_state_prob=0`.

## Controllers

| `macro_mode` | `macro_executor_version` | Policy chooses | Automated work |
|---|---|---|---|
| 0 | 0 | Each worker's primitive action; ordered market queue | Simulator rules only |
| 1 | 0 | Legacy macro intent, optionally sticky | Original planner, including its strategic heuristics |
| 1 | 1 | Macro intent, optionally sticky | Explicit executor; fixed batch of one; existing-farm upkeep |
| 2 | 0 | Macro intent, batch quantity, applicable region | Legacy structured executor and its heuristics/upkeep |
| 2 | 1 | Macro intent, batch quantity, applicable region | Explicit executor; existing-farm upkeep and routing |
| 3 | 0 | Job requests by class/region; ordered market queue | Assigns workers to requested jobs; routes and performs prerequisites |

Mode 3 does **not** automatically water, feed, harvest, invest, or sell. Its job
dispatcher reduces navigation decisions but leaves those priorities to the policy.
Modes 1/2 already automate more of that work, which can make them easier to learn.
Executor 1 is an alternative macro executor, not a general upgrade flag for modes 0/3.

`macro_decision_interval` applies to mode 1; modes 0/2/3 decide every turn.
`macro_score_features=1` exposes the older handcrafted candidate-value features
in modes 1/2. The default 0 exposes legality, not those estimates. This is an
independent observation ablation, not a reward switch.

Checkpoints and EMA companions have nine required sidecars: `.policy_version`,
`.obs_version`, `.executor_version`, `.hidden_size`, `.num_layers`,
`.param_alignment`, `.macro_mode`, `.macro_decision_interval`, `.macro_score_features`.
Keep them together. Viewer/evaluation and each populated opponent bank read their
own controller settings. Different modes/executors can face each other; trainable
and bank network dimensions must satisfy the existing bank-size rules. Loading a
learner for training still requires its configured controller to match.

## Simple starting rewards

Default targets are **3 total plots, 15 healthy viable animals, and 60 crops**.
`reward_target_crops=-1` computes the remaining tile capacity after animals;
explicit nonnegative crop targets are also supported.

| Setting | Default | Payment |
|---|---:|---|
| `reward_money_scale` | 1 | Net cash change / starting cash, paid immediately |
| `reward_growth_land` | 1 | Each new owned-plot peak, capped at 3 total plots |
| `reward_growth_crop` | 0.05 | Each new simultaneous healthy viable crop peak, capped at 60 |
| `reward_growth_animal` | 0.25 | Each new simultaneous healthy viable animal peak, capped at 15 |
| `reward_alive_daily` | 0.05 | Per game day at the full crop/animal targets; prorated each played turn |
| `reward_quality_scale` | 0 | Optional older sustained coverage/idle-land objective |
| `reward_pbrs_scale` | 0 | Optional potential-based guides |

Purchases reduce the immediate cash reward; sales increase it. With no discount,
the sum equals final minus reset cash. Discounting makes cash timing matter.
Growth bonuses are direct objectives, **not** PBRS: they are not taken back at
termination. Counts mean live, healthy, producible assets, not actions, inventory,
or empty structures. Replacing dead/harvested plants below the prior simultaneous
peak earns no new growth reward. Reset baselines and peaks start at the inherited
state, so loading a mature farm grants no free milestone payout.

The alive bonus is the mean of capped crop-target and animal-target fractions,
times `reward_alive_daily / turns_per_day`. Zero targets are omitted. Watering or
feeding an already-maintained producer is not separately rewarded.

Every reward coefficient can independently be set to zero. `reward_money_timing=0`
restores terminal cash; `reward_quality_timing=0` makes the optional quality
objective terminal. A value of 1 pays the respective increments during play.
The quality formula and four optional PBRS weights are described in the
[architecture reference](ENTITY_POLICY_V2.md). PBRS uses the trainer's gamma,
requires `train.reward_clip=0`, and sets the entire terminal potential to zero.
It can be combined with any controller and any direct reward settings.

Logs separate `cash_flow_reward`, `terminal_cash_reward`, all three
`growth_*_reward` values, `alive_reward`, quality rewards, and `discounted_pbrs`.
`score` remains actual terminal cash; `cash_gain`, `sold_units`, `sales_revenue`,
and `ending_shed_units` help distinguish growing from actually cashing out.

## Running experiments

Both plain training and the launcher use `config/kaggriculture.ini`. The launcher
only selects the requested controller, sets frozen controllers to inherit, and
generates a new output run ID. **It no longer overrides rewards, score features,
optimizer settings, reset mixtures, or `base.load_model_path`.** Use the explicit
`--dense-preset` flag to restore the reward table above; trailing arguments win.

Check the effective controller, checkpoint and rewards without allocating GPU
memory or starting training:

```bash
./puffer check kaggriculture
./puffer train kaggriculture
```

Training prints those settings and saves them in `logs/kaggriculture/RUN.start.ini`
before allocating the model. Set `base.run_id=None` for automatic new output IDs.
Reusing an output directory that already contains checkpoints is rejected rather
than overwriting old weights. Loading `.bin` weights is a warm start, not a restore
of optimizer state or elapsed training steps. `latest` searches across runs: use
an explicit checkpoint path when switching experiments. Its mode/executor/score
features must match your configuration; to change controllers, start fresh with
`base.load_model_path=None`. External opponent checkpoints carry their own modes.

A fresh mode-3 configuration is:

```ini
[base]
load_model_path = None
run_id = None
[env]
observation_version = 3
frozen_observation_version = 3
macro_mode = 3
macro_executor_version = 0
frozen_macro_mode = -1
frozen_macro_executor_version = -1
macro_decision_interval = 1
frozen_macro_decision_interval = -1
macro_score_features = 0
frozen_macro_score_features = -1
```

`-0` is zero, **not** inheritance. Executor 1 is invalid in mode 3. Score features
are handcrafted macro value hints for modes 1/2, not a general encoder-quality
switch; use 0 for a fresh mode-3 run. All controllers still use the same network
shape (404,440 parameters at H128/L3); this repair does not change the architecture.

```bash
# Structured macro executor, using rewards/load path from your INI:
bash ocean/kaggriculture/dense_experiment.sh 2 1 base.load_model_path=None
# Individual job requests, also using your INI rewards:
bash ocean/kaggriculture/dense_experiment.sh 3 0
# Legacy sticky macros, older candidate estimates, and optional PBRS:
bash ocean/kaggriculture/dense_experiment.sh 1 0 \
  env.macro_decision_interval=4 env.macro_score_features=1 env.reward_pbrs_scale=1
# Dense cash only:
bash ocean/kaggriculture/dense_experiment.sh --dense-preset 2 1 \
  env.reward_growth_land=0 env.reward_growth_crop=0 env.reward_growth_animal=0 env.reward_alive_daily=0
```

## Interpreting replay-start metrics

`money`/`score` remain final cash, including cash inherited at reset. `start_money`
and `cash_gain` separate those quantities. Production, successful plant/animal
placements, trades, deaths, watering coverage, and `land_purchases` now count only
progress **after reset**, not the expert's earlier replay activity. The original
KGState history is preserved for observations, market context and replay parity.
Ending assets (`plants_alive`, `animals_alive`, `ending_plots`, shed stock) are
still actual end-state totals; `start_plots` shows inherited land.

Actions and duration are accumulated over the same completed episode window;
`orders_per_turn` uses actual turns played, not the replay's absolute game step.
`root_*` and `reset_*` fields report separate conditional averages for non-bank
and replay-bank starts. `reset_fraction` gives their mixture; a zero source fraction
means there were no completed episodes for that source, not measured zero skill.
Market-opportunity classifications retain the game's historical demand context,
but their production/trade responses exclude inherited player activity.

These are diagnostic fixes, not reward changes. Cash/growth rewards already used
reset baselines. The defaults have no separate death/neglect penalty; purchases
reduce cash reward, and optional quality can penalize underused land. For a clean
cash-only comparison, set quality/PBRS/growth/alive coefficients to zero explicitly.

## Feasibility masks

Training and CPU viewers share the same sampled-prefix masks. An empty-item sale
is forbidden unless the selected worker actions deposit that item this turn.
Later market slots account for earlier sales, purchases, cash, and shed space;
quantity choices are capped for the selected command. Useless final-turn hiring,
redundant fertilizing, excess task requests/seeds, and ignored macro parameter
aliases are also masked. STOP and IDLE remain available. Choices of crop, species,
or profitable timing are not hardcoded into the new masks. Legacy executors keep
their pre-existing heuristics as part of those ablations.

Opponents' simultaneous hidden orders are not consulted; they may still change
prices or cause partial fills. These masks remove known infeasible requests, not
every strategically poor action.

PPO stores the mask **after** sampling. EMA applies local KL only on visited heads
for that prefix. Native `jsd` reports sampled-common-prefix conditional-head JSD,
not exact joint-policy JSD; that diagnostic requires homogeneous controller/input
semantics. Head-to-head leagues support mixed controllers.

`psro.sh` defaults to a separate `saved/kaggriculture_dense_v3` league. It copies
all nine raw/EMA sidecars, includes the controller in deduplication/payoff-cache
identity, and restores the selected learner's exact controller on `iterate`.
Mixed-controller PSRO uses payoff diversity; it explicitly marks behavioral JSD
unavailable instead of comparing incompatible action meanings. Historical
checkpoint leagues must stay separate from fresh v3 leagues.

```bash
make -C ocean/kaggriculture CC=clang native-test
make -C ocean/kaggriculture cuda-adapter entity-policy-test sampling-test
```

These are correctness checks, not evidence of stronger learned profits. Compare
matched seeds, training steps, reset mixture, and opponent panels when evaluating
controller and reward combinations.
