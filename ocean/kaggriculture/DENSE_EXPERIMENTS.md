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

The launcher leaves `config/kaggriculture.ini` untouched, supplies the v3/dense
preset, and generates a unique fresh run ID. Remaining arguments override the
preset. Other training hyperparameters and reset/opponent mixtures come from
your INI; use only fresh v3 external checkpoints, or disable that external pool.

```bash
# Explicit structured macro executor + dense rewards:
bash ocean/kaggriculture/dense_experiment.sh 2 1
# Individual job requests + the same rewards:
bash ocean/kaggriculture/dense_experiment.sh 3 0
# Legacy sticky macros, older candidate estimates, and optional PBRS:
bash ocean/kaggriculture/dense_experiment.sh 1 0 \
  env.macro_decision_interval=4 env.macro_score_features=1 env.reward_pbrs_scale=1
# Dense cash only:
bash ocean/kaggriculture/dense_experiment.sh 2 1 \
  env.reward_growth_land=0 env.reward_growth_crop=0 env.reward_growth_animal=0 env.reward_alive_daily=0
```

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
