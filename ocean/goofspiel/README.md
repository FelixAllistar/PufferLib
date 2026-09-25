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
checks 32,768 game transitions across single-policy and four-frozen-bank
layouts, on a non-default CUDA stream. Observations, legal masks, rewards,
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

Still pending: trainer exact-response pool refresh and
checkpoint persistence, and exploitability-driven sweep orchestration.
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
`env.exact_exploiter=1` explicitly fails until that integration is ported;
ordinary frozen-checkpoint selfplay works. Do not interpret a standard
upstream sweep's return metric as exploitability. The old robust-training
workflow remains preserved in `archive/5c-before-unification-20260924`.

Verification: core/adapter/exact-solver tests pass with ASan/UBSan; a bounded
4,096-step async FP32 training run with four frozen banks completes. The
evaluator's recurrent state and probabilities match upstream CPU inference
for that checkpoint. Interactive rendering still needs visual qualification.
