# Fresh entity policy and reward v2

The native trainer, frozen banks, match/league evaluator, and native CPU model
viewer use this architecture. Start a new run: old or untagged checkpoints are
rejected, not reshaped or converted. The game-state/reset-bank format is unchanged.
The two-day land-buy delay and mechanical task executor are unchanged.

## Reward contract

Only these two objectives pay at episode termination:

1. Cash: `reward_money_scale * (final_cash - actual_reset_cash) / starting_money`.
   The reset cash baseline is constant within an episode, so maximizing this is
   maximizing final cash. Inherited cash from a reset bank is not counted as earnings.
2. Quality: `reward_quality_scale * (coverage_sum - idle_cost * idle_sum) / episode_steps`.
   Sums start at the actual reset, not at the beginning of the inherited game.

For each plot, `u[q]` is the fraction containing healthy, viable crops or animals.
Empty structures, weeds, locked tiles, and unused inventory do not count. Health
uses the simulator's watering/feeding risk state. Viability means harvestable
yield exists, or the producer has a remaining maturation/production opportunity
before the last actionable day. A newly planted one-shot crop's internal
`yield_units=1` is **not** mistaken for already harvestable produce.

At each transition:

```
coverage = (u[0] + min(u[0:2]) + min(u[0:3]) + min(u[0:4])) / 4
idle = sum(1 - u[q] for extra owned plots q) / 3
```

This rewards sustained productive expansion. Buying empty land incurs an idle
charge; extra plots cannot conceal an abandoned earlier plot. The initial plot
has no idle charge. Quality is season-normalized and bounded, not a high-water
counter. A short, late reset episode can earn only its played fraction of a
season's quality reward. Cash remains unbounded and linear.

Recommended starting settings (the remote active INI is maintained separately):

```ini
[env]
observation_version = 2
frozen_observation_version = 2
macro_mode = 3
frozen_macro_mode = -1
macro_executor_version = 0
frozen_macro_executor_version = -1
reward_money_scale = 1
reward_quality_scale = 1
reward_quality_idle_cost = 0.25
reward_pbrs_scale = 0
pbrs_cash_weight = 1
pbrs_stock_weight = 1
pbrs_crop_weight = 0.25
pbrs_animal_weight = 0.25
```

No maintenance-action, phase, peak-expansion, fitted-progress, per-step cash, or
win bonus is added. Nonzero retired reward activation scales and tagged-curriculum rewards
are rejected. `score` and `sweep_score` now report actual terminal cash.

## Optional PBRS experiment

Set only `reward_pbrs_scale = 1` to try the default guides. Potential is:

```
Phi = cash_weight * (cash - reset_cash) / starting_money
    + stock_weight * exact_current_shed_sale_proceeds / starting_money
    + crop_weight * healthy_viable_crop_count / 25
    + animal_weight * healthy_viable_animal_count / 25
r_shape = reward_pbrs_scale * (train.gamma * Phi(next) - Phi(now))
```

The **entire** terminal potential is forced to zero. There is no independent
shaping gamma: the trainer binds its gamma before constructing environments.
PBRS with nonzero `train.reward_clip` is rejected. Its discounted episode return
is the start-dependent constant `-reward_pbrs_scale * Phi(reset)`, including
reset-bank starts. It changes credit assignment, not the two terminal objectives.

Stock uses exact current sale quotes, not spot-price multiplication or a fitted
liquidation factor. No future output quantities or future prices are forecast.
Unused land, seeds, and unplaced animals receive no potential credit. The four
weights are explicit experimental guides, not claimed learned asset values.

Compare PBRS off/on with the same initialization, reset mixture, evaluation
seeds, and opponent panel. Quality scale is a separate objective tradeoff;
changing it is not an invariant shaping experiment.

## Observation and network contract

Observations contain 1,424 floats, with uncapped scaled numeric features:

| Group | Shape | Offset |
|---|---:|---:|
| Globals | 128 | 0 |
| Products | 9 × 56 | 128 |
| Plots (own four, opponent four) | 8 × 24 | 632 |
| Own workers | 17 × 32 | 824 |
| Mechanical task availability | 56 | 1368 |

Episode progress remains at index 3 for EMAG. Cash has uncapped magnitude,
residual, and eight-bit limb features; the limbs survive bf16 observation storage
exactly. Opponent inventories/seeds are not exposed. Opponent public worker
positions and farm/product summaries are exposed. Route absence has a distinct
`-1` sentinel; `(0,0)` remains a valid route destination.

Products include identity, current prices/inventory, own shed/held/ready stocks,
producer state, and exact counterfactual sale proceeds/post-sale prices for:

- Actual order quantities: `1, 2, 3, 4, 5, 6, 8, 10`.
- Bulk quantities: `1, 5, 10, 20, 30, 50, 75, 100`.

Quotes assume no simultaneous opponent trade or subsequent town consumption.
They are exact for that isolated sale, not predictions of joint execution.
They are not clipped to current holdings; holdings are separate features. A
cache keyed by product and exact market inventory avoids recomputing an unchanged
curve and is shared by both player observations.

Network (two-layer ReLU MLPs; trainable biases in augmented matrix columns):

```
globals + tasks: 184 -> 64 -> 64
each product:    56 -> 32 -> 32   (shared across nine products)
each plot:       24 -> 32 -> 32   (shared across eight plots)
each worker:     32 -> 16 -> 16   (shared across seventeen workers)
concatenate:    880 -> H  -> H
recurrent:      existing N-layer MinGRU
task branch:      H -> H/2 -> 748
market branch:    H -> H/2 -> 310
value branch:     H -> H/2 -> 1
```

`H` must be divisible by eight. Augmented matrix columns are padded to multiples
of eight, with zero-valued padding inputs. This keeps parameter and gradient
storage gap-free for the native flat optimizer and checkpoint serialization;
padding is not an extra observation or learned entity feature.

The action interface remains 47 heads / 1,058 logits. The existing conditional
market queue, legality masks, and task executor remain in charge of execution.
The market branch is separate but is **not** a new autoregressive neural decoder.
Plot summaries are not an exact tile-grid encoder.

Every checkpoint and EMA companion carries strict `.policy_version`,
`.obs_version`, `.executor_version`, `.hidden_size`, `.num_layers`, and
`.param_alignment` metadata. Keep these sidecars with copied checkpoints.
Native CPU inference reads the declared layout; no architecture inference from
file size or legacy linear-policy path is used for Kaggriculture.

The offline C view exposes `kg_policy_entity_observation` and
`kg_policy_entity_mask` with a live `Env` context. Retired stateless byte-view
symbols are removed: a bare `KGState` cannot reconstruct reset reward history
or land-fill timers. Old byte BC/export tooling must not be used for these weights.

## Validation

```
make -C ocean/kaggriculture CC=clang native-test
make -C ocean/kaggriculture cuda-adapter
make -C ocean/kaggriculture entity-policy-test
bash build.sh kaggriculture --gpu
```

The policy test compares serialized CPU inference, GPU rollout, and recurrent
training outputs (including an episode boundary), checks all parameter/gradient
registrations, probes full BPTT numerically, and verifies a descent update.
The adapter test compares both simulators' game state, masks, rewards, new
observations, reward history, land timers, reset behavior, and logs.
