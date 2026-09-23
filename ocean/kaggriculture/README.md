# Kaggriculture migration

This contains the rule simulator, standalone 2/2 policy contract and upstream
GPU adapter/sampler integration. The current config is a **qualification profile**
using upstream's stock network, not the old entity encoder or a tuned baseline.
Keep using the preserved old runtime for existing checkpoints and experiments
until the remaining migration boundaries below are qualified.

`core.h` combines the old simulator's declarations and implementation into one
header, following `SKILL_ISSUES.md` and the header-owned implementation pattern
in `ocean/trianglepath/trianglepath.h`. Its host/device qualifiers follow the
pattern in `ocean/robot_arm/robot_arm.h`. No shared trainer file is changed by
this port. The rule core has no PufferLib, rendering, policy, or reward dependency.

The native rules, structured worker/market actions, seeded daily randomness,
cash/production counters, JSON snapshots, reactive reference bot, and resumable
state layout are preserved. This includes the PLACE-to-shed fix at locked shed
corners and simultaneous market quoting. Reset-bank state format remains version
1 with the same layout; this does **not** establish neural checkpoint compatibility.

The old implicit repair of invalid configuration is replaced by assertions.
Valid settings retain their behavior. Gameplay legality checks still implement
invalid actions as no-ops; snapshot import validation still rejects malformed
input without modifying the destination. Neither is a config-repair fallback.

## Controller and observations

`policy.h` consolidates the current 2/2 executor, entity observations and mask
rules into one implementation header, following `SKILL_ISSUES.md`. It depends
only on the rule core, not the old trainer, renderer, rewards, bots or `Agent`
layout. The caller owns the game and action/observation buffers. Single-use
helpers and unused farm-summary work are removed; worker ordering and tie
breaks are preserved. No planner strategy or learning algorithm is changed.

This port supports controller 2/2 with score features off, observation v3 and
policy ABI 5: 1,424 float observations, 47 action heads and 1,978 logits. Five
concurrent intent/count/region requests share workers with automatic chores;
the ordered market queue retains exact quantities 1-100 and feed overrides.
Raw mode, 2/1, retired modes 1/3, frozen-bank metadata and opening curricula are
not included in this commit. Existing models still require the old runtime.

`KagPolicy` contains only controller settings, quote caches and observation
history. Initialize it with explicit `market_slots` (1-10), `max_hands` (1-16),
and `land_buy_min_days` (normally zero). The controller assumes the competition
10x10 board; the independent rule core still supports smaller boards.
Call `kag_policy_reset` after a fresh or restored game, then `kag_policy_step`
once after each transition, including the terminal one. Episode cash/time,
coverage/idle sums and producer peaks remain observable even without shaped
rewards. They do not themselves issue rewards or preload a critic.

`kag_write_observation`, `kag_write_mask` and `kag_decode_multi_action` expose
the standalone contract. The legacy environment writes observations before
refreshing the base masks; retain that order when integrating the optional
land-delay history. The CPU sampler preserves deterministic/stochastic actions
and its RNG advancement. Prefix `begin/before/commit` functions track worker,
seed, cash and shed reservations without consulting hidden opponent orders.
The GPU sampler calls these environment-side rules before each categorical draw
and saves the selected-prefix masks in the existing rollout buffer. Unvisited
market heads store action zero and a singleton mask, consume no RNG and have
probability one, zero entropy and zero gradient under the **unchanged upstream
PPO loss**. Forced singleton worker heads still consume their legacy RNG draw.
Masks use the ordinary upstream precision tensor; no bitpacking was added.

## Upstream GPU adapter

Build with `NVCC_ARCH=sm_120 bash build.sh kaggriculture --cu` on Vast (choose
the architecture appropriate to another GPU). The `--cu` flag is required;
there is no native CPU trainer adapter or renderer yet. The standalone CPU
rule/controller tests remain available.

`kaggriculture.cu` follows `ocean/admiral/admiral.cu`: a device batch of games,
one learner row per controlled seat, bound CUDA stream and separate step and
observation kernels. It uses the existing `puf_*` API without modifying
`src/pufferenv.h`. `src/pufferl.cu` gains only the sampler's game/row context
and a Kaggriculture-specific prefix-mask hook. No network, loss, optimizer,
advantage calculation, scheduling or shared environment API is replaced.

`env.num_agents=2` controls both seats with the learner (mirror play, **not** a
checkpoint league). With `env.num_agents=1`, `env.learner_seat=0` or `1` faces
`env.bot_policy=0` (pass) or `1` (the native reactive rule bot). Use separate
evaluation invocations per bot: upstream's post-training bot ladder does not
switch GPU bot policies, and its multi-policy/selfplay guard remains intact.

The adapter uses the competition's default game rules, fresh starts only,
and one explicit reward: on termination,
`reward_money * (ending_cash - starting_cash) / 3000`. This is not the old
shaped reward mix. Stock upstream still clips these rewards to `[-1, 1]` before
learning; a clipping decision is required before meaningful cash optimization.
Logged cash and episode returns are the **unclipped environment values**.
The provided short config is for wiring checks, not a production training run.

At termination it emits the terminal reward/flag, records the finished game,
resets the game/controller history and writes the next episode's observation.
An explicit reset also clears logs and reward/terminal buffers. Episode seeds
advance via a per-game LCG initialized from the game index, rather than keeping
the same daily randomness forever. This is a new adapter seeding schedule,
not a claim of identical legacy training trajectories.

## Checks

```bash
uv run --no-project --with pytest python -m pytest -q \
    ocean/kaggriculture/tests/test_core.py ocean/kaggriculture/tests/test_policy.py

# Optional migration comparison against the preserved source checkout:
KAGGRICULTURE_REFERENCE_ROOT=/path/to/old/PufferLib \
    uv run --no-project --with pytest python -m pytest -q \
    ocean/kaggriculture/tests/test_core.py ocean/kaggriculture/tests/test_policy.py
```

Tests compile optimized and AddressSanitizer/UndefinedBehaviorSanitizer builds.
They independently check market quotes, cow maturity/care, PLACE-to-shed, seed
reservations, neglect, terminal timing, snapshot round trips, and Python's RNG.
The optional old/new comparison checks full-state byte hashes at 69,024
transitions across 96 episodes, plus 2,784 daily JSON snapshots. It covers
random/invalid structured actions and the reactive bot, two resets per seed,
board sizes 4–10, two cash budgets, two shed capacities, weeds on/off, and free
or paid hiring. No GPU work occurs in these tests.

`tests/test_core.c` also contains a CUDA kernel for compile qualification with
NVCC (`-x cu -c`). Compilation alone is not GPU execution/parity qualification.
The existing old-tree `parity.py` can target a shared library built from this
header; it has also passed against the installed Kaggle 1.32.7 interpreter.

Controller tests cover concurrent chores/strategy, shared seeds/housing, exact
ordered trades, same-turn shed transfers, safe delivery overflow, feed controls,
fertilizer-before-water, explicit harvest/wait, queue/hand limits, optional land
delay, quote-cache invalidation and reward-independent observation history.
The optional old/new comparison checks hashes of both players' observations,
base/prefix masks, chosen actions, decoded commands and full resulting states,
plus exact sampling RNG state: 8,876 transitions / 17,752 player decisions over
12 full games and 64 constructed farm starts. These are synthetic fixtures,
not replay-dataset coverage measurements. Both optimized and sanitized builds
pass. `tests/policy_compile.cu` also compiles the controller/observation/mask
path for `sm_80`; no GPU execution or speedup is claimed.

`tests/test_gpu.cu` includes the actual upstream trainer, so its tests exercise
the production sampler, importance/log-prob cache and PPO gradient kernel.
The opt-in runner is (omit the trainer variable to run only the 20 kernel/adapter
checks):

```bash
KAGGRICULTURE_GPU_TEST_BINARY=build/kag_gpu_test_fp32 \
KAGGRICULTURE_TRAIN_TEST_BINARY=build/kag_adapter_fp32 \
    uv run --no-project --with pytest python -m pytest -q \
    ocean/kaggriculture/tests/test_gpu.py
```

Compile the test with the same includes, libraries, precision and environment
defines as the native build, substituting `ocean/kaggriculture/tests/test_gpu.cu` for
`src/pufferl.cu` and omitting `PUFFERLIB_BUILD_MAIN`. Repeat in BF16. The tests
cover single/both seats, odd row offsets, CUDA graph/non-graph execution,
CPU-reconstructed prefix masks and categorical probabilities, exact Philox
advancement, inactive-head entropy/gradient and two full episodes plus a
late-state termination fixture. They are not enabled by a normal CPU test run.
Four native trainer cases additionally check finite changing weights and exact
zero-learning-rate warm starts in sync/async and graph/non-graph modes. These
tiny stock-network runs qualify wiring, not learning quality or throughput.

Qualification on Vast's RTX 5060 Ti (`sm_120`): **24/24 pass in FP32 and 24/24
in BF16**. The separate upstream Chain Reaction warm-start suite also passes
**19/19 in each precision**. No local GPU was used. The original runtime is
still required for legacy experiments; this does not qualify the missing
entity network, resets, BC or checkpoint leagues.

## Remaining integration

Port and qualify the entity encoder/decoder and checkpoint identity. The
adapter currently uses the stock network intentionally, not a silent fallback.
Resolve terminal-cash clipping explicitly before training: stock upstream clamps
rewards to `[-1, 1]`. Replay resets, BC/value initialization, checkpoint identity,
external leagues and GPU multi-policy execution remain separate changes.
No old PPO loss, optimizer, eMAG, QD, packed masks, or performance experiment is
included here.

## Source baseline

Preserved `5c` checkout at `3fa5d14cc`, as inspected on 2026-09-23:

- `kaggriculture_core.h` SHA-256:
  `1fe79cff3b3c64d40f301ac308d2c93e2772d0022a71b894ad4daf612e4fa9a8`
- `kaggriculture_core.c` SHA-256:
  `bd221961767f0954983bdc6a798ddee09d914cefe594125f0fdd6dbbc803e150`
