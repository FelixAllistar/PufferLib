# Entity-v3 BC / critic / multi-intent pilot — 2026-09-20

Historical policy-v3 / 2/1 pilot below. In the isolated `pufferlib-multi-intent`
checkout, see [MULTI_INTENT_PILOT.md](MULTI_INTENT_PILOT.md) for the current
policy-v5 / 2/2 experiment. Its source contracts and datasets are separate.

## Status and location

Work is LOCAL in `/home/felix/puffertank/pufferlib`. The live remote checkout
`/workspace/PufferLib`, running processes, rewards, binaries and checkpoints
were not changed. No GPU training job was started and no BC checkpoint exists
from this pilot yet. The CUDA trainer compiles; GPU forward/backward and
closed-loop performance still need a free compatible GPU.

Implemented:

- Mode 2 / executor 1 / observation-policy v3, H512/L3 frozen pilot profile.
- Production stateful float observations, teacher-prefix legality masks,
  original two-player replay actions, and production reward reconstruction.
- Float BC dataset v3; strict source/controller/reward/gamma/dimension checks.
  Old byte datasets, old generator/DAgger modes and unsafe reset-column surgery
  are rejected. Train/validation membership is fixed by episode, not resampled.
- Optional joint critic loss; checkpoint architecture/controller sidecars.
- Separate full multi-intent annotation sidecar. Current 2/1 does not read it.
- Local PLACE-to-shed correction in the core, primitive masks and market-prefix
  simulator, including animal returns from locked shed-access corners.

The multi-intent POLICY/EXECUTOR is not implemented/enabled yet. Rich labels
are now retained so the next controller can be built without reparsing the big
official JSONs or silently changing what existing 2/1 weights mean.

## Current data

Base directory: `/home/felix/puffertank/elite_replays/bc_prep_2026-09-20/`.

- `majkel_fixed/summary.json`: all 16 pilot games passed all 720 frame checks
  after the core correction; the four original cow/shed failures now pass.
- `entity_2_1_multi_pilot_v3.bc`: 13 training / 3 held-out Majkel1337 games.
  1,656 training / 378 held-out conservative single-intent actor labels.
  11,504 nonterminal value targets, plus terminal rows with NaN targets.
- `entity_2_1_multi_pilot_v3.intents.jsonl.gz`: all 11,504 teacher actions,
  original ordered market queues, grouped observed production requests,
  worker positions, chore/movement traces, and single-2/1 projection diagnostics.
  Every stored raw action was compared to the original cached tape and matched.
- `entity_2_1_multi_pilot_v3.json`: frozen profile, hashes, episode membership,
  source records, label counts and approximation warnings.

Generating this pilot from compact tapes took about 21 seconds. Earlier
`entity_2_1_pilot_v3*` files are intermediate revisions, not the current input;
the trainer rejects their old source fingerprints.

Single-intent labels are deliberately conservative. Native decoder previews
must match the observed strategic work types and market totals, but worker
assignment/pathing need not match. Unidentified parameter heads get no actor
gradient. Unidentified rows still contribute critic targets and recurrent
context. Latent macro history is a canonical compatible projection, not known
ground truth. These are warm-start experiments, not proven strong clones.

## Why multiple intents matter

An actual pilot turn contains `SELL WHEAT`, four `HIRE` orders, `BUY_ANIMAL COW`
and `BUY_ANIMAL SHEEP` together. One macro cannot express that combination.
Observed production groups range from zero to four in this pilot; market queues
range from zero to ten orders. Repeated orders can sometimes already be covered
by one macro quantity, so a multi-order count is NOT an impossibility rate.

Recommended next controller: several production requests plus an ordered market
queue, sharing one worker/resource planner and automatic upkeep. Water/feed/care,
routine harvesting, collection and routing should not all become compulsory
learned decisions again. Joint resource masks must account for earlier requests,
and upkeep must run once, not once per requested macro. Use a NEW controller
version; never reinterpret an existing 2/1 checkpoint in place.

We can preserve 100% of observed primitive actions, but cannot promise 100%
imitation with a simplified controller. Exact worker-action matching would also
require routing/maintenance overrides. Movement destinations and intended animal
species behind a bare pasture-building action are unknown; annotations do not
invent them. Current labels retain the original command as the source of truth.

## Joint critic math

`G[t] = reward[t] + gamma * G[t+1]`, with zero future return after termination.
Reward comes from `kag_reward_step`, not a repeated final-cash score. Loss:

`actor_cross_entropy + value_coef * mean((V - G)^2) / (2 * max(1, train_return_variance))`

Targets stay in PPO reward units; only loss magnitude is normalized. The
variance uses TRAIN episodes only. `bc.value_coef=0` is actor-only BC;
`bc.value_coef=0.1` is a starting joint-loss ablation, not a tuned optimum.
The critic initially estimates expert-continuation returns, not returns for a
random policy or arbitrary actions. PPO must recalibrate it after cloning.

Pure BC previously had action labels but no return targets. Zero critic gradient
is correct for that objective; removing the zero without adding a real value
loss would not help. The critic is not consulted when a standalone BC actor
chooses actions. This alone does not explain old bad clone scores.

Reset-start PPO still trains both actor and critic on the learner's subsequent
rollouts; network weights are not reset. This first offline pilot uses complete
opening-start games. Reset-start offline targets need fresh reward baselines
and recurrent resets and are not silently substituted from full-game returns.

## Commands (repo root)

Local Python environment was prepared using uv; no global package replacement:
`/home/felix/puffertank/.venv-kag-bc-audit` (numpy plus Kaggle source, no heavy
Kaggle dependency tree). Upstream PyPI latest was 1.32.7; its Kaggriculture Python
source was byte-identical to GitHub master when checked. This was a native
operation-order bug, not a newly changed upstream game.

Build into an isolated directory. The local audit used `NVCC_ARCH=sm_86` for
compile/preflight only; on the actual training host use a compatible architecture:

```bash
NVCC_ARCH=native make -C ocean/kaggriculture CC=clang BUILD=build/bc_entity \
  build/bc_entity/libbc_replay.so build/bc_entity/kag_bc
```

No-GPU preflight, with explicit current data and frozen profile:

```bash
ocean/kaggriculture/build/bc_entity/kag_bc \
  bc.profile=ocean/kaggriculture/bc_2_1_pilot.ini \
  bc.data=/home/felix/puffertank/elite_replays/bc_prep_2026-09-20/entity_2_1_multi_pilot_v3.bc \
  bc.verify_only=1 bc.value_coef=0.1
```

Training uses the same command with `bc.verify_only=0`, an explicit NEW
`bc.output` path and chosen epochs. Compare separate `value_coef=0` and `0.1`
outputs. Do not start on the occupied remote GPU without scheduling a slot.
The pilot defaults to one full recurrent episode per batch. It refuses to
overwrite a checkpoint. Validate GPU gradients and closed-loop scores before
scaling to the full teacher pool or handing a checkpoint to PPO.

Checks passed: 13 entity/label/bridge/preflight Python tests (including disabled
GPU), 7 replay-preparation tests; native shed, action-prefix, entity-reward,
observation/controller and land-delay tests. GCC's existing compact executor
code triggers `-Werror=misleading-indentation`; native suites were built with
clang. No existing executor formatting was changed just to suppress that warning.
