# Goofspiel on PufferLib 5.0

The CPU simulator, observations, legal-card masks, renderer and standalone
exact exploitability evaluator are ported from `5c` at `036cf4251`.
The simulator and observation code are unchanged. The CPU adapter uses the
existing `MY_VEC_INIT` interface to assign four frozen banks consistently
across rollout buffers; no shared trainer changes are needed.

```bash
make -C ocean/goofspiel test sanitize viewer exploit
./build.sh goofspiel puffer_goofspiel --float
./puffer_goofspiel train
./ocean/goofspiel/build/exploit uniform
./ocean/goofspiel/build/exploit PATH.bin --policy.hidden_size=32 --policy.num_layers=2
./ocean/goofspiel/build/viewer
```

Run commands from the repository root. Omit `--float` on GPUs supporting the
default precision. This trainer uses CPU simulation and GPU inference/PPO.
For GPU simulation, build with `./build.sh goofspiel puffer_goofspiel_gpu --cu --float`.
The GPU adapter uses the optional environment setup hook to bind legal-card
masks and group player rows by policy. Keep `vec.num_buffers=1` for GPU simulation.
The standalone evaluator uses
CPU inference and supports perfect-information, zero-sum games up to five
cards (the default compiled observation/action ABI is four cards).

The config retains the old ordinary gameplay and supported PPO settings, with
upstream selfplay-pool keys. Old EMAg, priority-replay and epoch-sampling
settings are not imported into the new optimizer. This is not an identical
training algorithm to the old fork.

`make -C ocean/goofspiel test-gpu NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=sm_61`
checks 65,536 game transitions across single-policy and four-frozen-bank
layouts, including exact responses with direct launches and CUDA graphs,
on a non-default CUDA stream. Observations, legal masks, rewards,
terminals, final state, logs and RNG match host execution exactly. CPU tests
and sanitizers pass; native FP32 GPU training completes 4,096 steps with graphs
off/on and four frozen banks. These are correctness/smoke checks, not learning
quality, performance scaling, BF16 or interactive-renderer qualification.

The published trainer also builds from the independent GitHub checkout at
`2e39262cc`. Its 4,096-step, four-bank smoke passes with graphs off/on and
verifies that training changes checkpoint weights. Reproduce on an idle GPU:

```bash
GOOFSPIEL_TRAIN_BINARY=./puffer_goofspiel_gpu \
    uv run --no-project --with pytest \
    python -m pytest -q ocean/goofspiel/tests/test_native_training.py
```

The standalone CUDA exact evaluator is also available without any trainer
or GPU-simulator hook changes:

```bash
make -C ocean/goofspiel exploit-gpu NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=sm_61
./ocean/goofspiel/build/exploit_gpu uniform
./ocean/goofspiel/build/exploit_gpu PATH.bin --policy.hidden_size=32 --policy.num_layers=2
GOOFSPIEL_EXACT_GPU=ocean/goofspiel/build/exploit_gpu \
    uv run --no-project --with pytest --with numpy \
    python -m pytest -q ocean/goofspiel/tests/test_exploit_gpu.py
```

Choose `CUDA_ARCH` for your GPU. This solver reads native flat FP32 standard
network checkpoints, not custom encoders. CPU/CUDA exploitability agrees
within 5e-7 for generated weights and the native H32/L2 training checkpoint,
covering two through four cards, prize orders, tie rules, return types and
truncated games. Uniform-policy known values and truncated-file rejection
also pass (12 tests per checkpoint). Larger games and other precisions have
not been qualified. Exact enumeration grows rapidly with game size.

The legacy behavior-distance report is now a subcommand of the same binary:

```bash
./ocean/goofspiel/build/exploit_gpu behavior A.bin B.bin uniform \
    --policy.hidden_size=32 --policy.num_layers=2
```

It emits exact exploitability per checkpoint and pairwise Jensen–Shannon
divergence (base 2), its square-root distance, and total variation. These are
equally averaged over enumerated decision histories, not weighted by their
frequency under either policy. All checkpoints must share the supplied
architecture and game rules. The additional behavior test checks identical
policies, symmetry, bounds, square-root consistency, and CPU exploitability
agreement. This report alone is not population selection or PSRO.

Use `scripts/goofspiel_behavior.py REPORT.tsv --strategy=farthest --max-policies=16`
to group similar policies and select representatives. It writes `_groups.tsv`
and `_selected.tsv` beside the report, replacing those outputs if reused.
`best` orders by lowest exploitability subject to minimum distance; `farthest`
starts with the least exploitable policy and maximizes distance to the selected
set, without enforcing `--minimum-distance`. Groups use transitive similarity,
not an all-pairs distance bound. This does not modify training or a live league.
Unit/CLI tests and a native-checkpoint report-to-selection smoke pass.

The legacy offline response-pool writer/loader passes a native-checkpoint
round-trip test: populate a three-slot reservoir with seven responses, save
and restore all table bytes, continue eight more updates identically, then
reload over populated tables. This does not integrate refresh into training.
Its sidecar format does not fingerprint every game-rule setting; do not reuse
response tables across different rules. Run the test with an H32/L2 checkpoint:

```bash
make -C ocean/goofspiel build/test_exact_pool NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=sm_61
pool_test_dir=$(mktemp -d)
./ocean/goofspiel/build/test_exact_pool PATH.bin "$pool_test_dir/checkpoint"
```
Native CPU/GPU-simulator training supports `env.exact_exploiter=1` with
selfplay enabled. The first `exact_exploiter_banks` frozen banks use exact
response actions; other banks retain their neural policies. The CUDA solver
refreshes the latest response at the initial and subsequent checkpoint saves.
`exact_exploiter_history` bounds the reservoir, and
`exact_exploiter_current_prob` controls latest-versus-history sampling per game.
This is not PFSP or a change to PPO losses/optimizers. Leave it disabled for
ordinary frozen-checkpoint selfplay. GPU simulation preallocates all response
history slots on the first upload. Refreshes synchronize at checkpoint
boundaries and update device metadata, so already-captured graphs see new
tables without retaining freed pointers. The parity test grows and replaces
the pool after capture, checks that all three test tables are used, and
compares exact-response traversal indices as well as simulation outputs.

Each checkpoint has a `.bin.exact` sidecar. Loading weights restores that pool
when present, preserves it at the new run's initial save, then resumes refreshes.
A missing sidecar starts a new pool. Use the same game rules and pool capacity
when continuing; this is not optimizer or in-flight episode restoration.
Evaluation uses neural policies without substituting response-table actions.
Eight native tests cover CPU/GPU, sync/async and graphs off/on, pool saturation,
byte-identical initial restoration, continued refreshes, and post-training eval:

```bash
GOOFSPIEL_EXACT_TRAIN_BINARY=./puffer_goofspiel \
GOOFSPIEL_EXACT_GPU_TRAIN_BINARY=./puffer_goofspiel_gpu \
    uv run --no-project --with pytest \
    python -m pytest -q ocean/goofspiel/tests/test_native_training.py
```

### Independent seeded population training

The environment-local launcher runs independent seeds sequentially, with the
normal native dashboard visible. It uses the legacy population's supported PPO
hyperparameters and 64×1 architecture, but **not** its EMAg losses or optimizer.
Each member builds its own checkpoint selfplay/exact-response history; members
do not train against each other. This is not PFSP or population promotion.

```bash
GOOFSPIEL_TRAIN_BINARY=./puffer_goofspiel \
    bash ocean/goofspiel/train_population.sh 16 1001 population64x1
# Optional native settings follow count, first seed and prefix:
GOOFSPIEL_TRAIN_BINARY=./puffer_goofspiel_gpu \
    bash ocean/goofspiel/train_population.sh 4 2001 trial \
    --train.total_timesteps=2000000 --env.exact_exploiter_history=32
```

The default budget is eight million steps per member. Override the checkpoint
root with `GOOFSPIEL_CHECKPOINT_DIR`. All destination run directories are checked
before training starts; existing runs are not overwritten. A failed member
stops the launcher. Run IDs, seeds, checkpoint root and fresh initialization are
owned by the launcher and cannot be overridden in the trailing arguments.
Other native settings override its defaults.

Training does not automatically launch population evaluation. Run it explicitly
after training, using the same architecture and game rules:

```bash
GOOFSPIEL_TRAIN_BINARY=./puffer_goofspiel_gpu \
    bash ocean/goofspiel/eval_population.sh population64x1 262144 0.002 best
```

The four positional arguments are prefix, minimum games per directed match,
cycle threshold above 0.5, and mode. `best` selects the least exploitable saved
checkpoint per seed; `all` matches every checkpoint; `scan` only writes exact
exploitabilities; `jsd` writes the behavior-distance report. Selection reads
`.bin` files, never retired `.emag` sidecars. Trailing native settings apply to
both the solver and matches; default architecture is 64×1. Use
`GOOFSPIEL_EXACT_GPU` for a different solver executable and `GOOFSPIEL_LOG_DIR`
for a different report root. Reusing a report prefix replaces its TSV reports,
not checkpoints. Headless native matches average both player orderings; draws
are reported separately. Rollout boundaries can exceed the requested game
budget. Cycles are sampled candidates, not statistical significance claims.

Build a current trainer: its `CUDA_EVAL` result now includes the already-computed
match draw rate. Older binaries without that field are rejected. No match
calculation or PPO/optimizer implementation changed. Ordinary rendered GPU
evaluation still needs qualification: the host probe's close currently hits
the GPU-vector free path; the population wrapper explicitly uses `--headless`.

Eight launcher contract tests and two real 1,024-step 64×1 member runs pass:
initial seeds differ, weights update, frozen-bank shapes follow the learner,
and every checkpoint has an exact-response sidecar. Reproduce with:

```bash
GOOFSPIEL_TRAIN_BINARY=./puffer_goofspiel_gpu \
GOOFSPIEL_EXACT_GPU=./ocean/goofspiel/build/exploit_gpu \
    uv run --no-project --with pytest \
    python -m pytest -q ocean/goofspiel/tests/test_population.py
```

Evaluation tests cover all four modes, checkpoint selection, single-checkpoint
members, a known three-policy cycle, EMAg exclusion and subprocess failures.
All four modes also pass against the two real native populations, including
complementary payoff entries and symmetric, bounded draw matrices. The native
match budget is only 32 games in these tests, not a policy-quality evaluation.

### Exact-exploitability sweeps

The environment publishes `exploitability` (latest saved checkpoint) and
`best_exploitability` (minimum across checkpoints saved in this run). Select
either metric explicitly; ordinary `score` is still game points, not exact
exploitability. Smaller is better. The checkpoint hook measures the metric
even when exact-response opponents are disabled, without creating table pools.
When they are enabled, it reuses the response solver's result.

```bash
./puffer_goofspiel_gpu sweep \
    --sweep.metric=best_exploitability --sweep.goal=minimize \
    --sweep.downsample=1 --sweep.max_runs=40 --sweep.gpus=1 \
    --base.eval_episodes=0 --selfplay.eval_games=0 --selfplay.eval_bot_games=0
```

Disable sampled post-training evaluations for this metric: they evaluate a
different objective. A single returned metric avoids bin-averaging checkpoint
measurements. `base.checkpoint_interval` controls measurement frequency, so
use the same interval when comparing trials. `Exact checkpoint:` output names
each measured model; models are retained, not automatically promoted. A resumed
run measures its initial checkpoint afresh rather than importing a prior best.
For standalone checkpoint evaluation use `exploit_gpu`, not `puffer eval`
with an exact metric selected.

The Goofspiel sweep ranges include its starting horizon/lambda/clip values and
cap width at the solver's supported 256; other ranges remain upstream's.
This restores the objective, not the old EMAg optimizer or its search space.
Four two-trial native sweep tests compare Protein's returned latest/best scores
against the standalone solver on every saved checkpoint, both with and without
exact-response opponents. Enable them with `GOOFSPIEL_EXACT_GPU_TRAIN_BINARY`
and `GOOFSPIEL_EXACT_GPU` when running `test_native_training.py`.

Verification: core/adapter/exact-solver tests pass with ASan/UBSan; a bounded
4,096-step async FP32 training run with four frozen banks completes. The
evaluator's recurrent state and probabilities match upstream CPU inference
for that checkpoint. Interactive rendering still needs visual qualification.
