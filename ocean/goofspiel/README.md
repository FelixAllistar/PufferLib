# Goofspiel on PufferLib 5.0

The CPU simulator, observations, legal-card masks, renderer and standalone
exact exploitability evaluator are ported from `5c` at `036cf4251`.
The simulator and observation code are unchanged. The CPU adapter uses the
existing `MY_VEC_INIT` interface to assign four frozen banks consistently
across rollout buffers. Automatic evaluation and sweep validation are
Goofspiel-specific hooks; other environments keep their evaluation behavior.

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

## Everyday training, exact evaluation, and sweeps

Build GPU simulation and GPU training as the normal executable. Both models
are included:

```bash
bash build.sh goofspiel --cu --float
./puffer train
./puffer eval latest
./puffer sweep
```

No sweep helper or extra metric flags are needed. `config/goofspiel.ini` selects
PufferNet by default. Change `policy.architecture` to `transformer` for the
second experiment, or pass `--policy.architecture=transformer`.

Training automatically reports exact `exploitability` and `best_exploitability`
on the dashboard and prints both at the end, with final/best checkpoint paths.
The opponent is optimized against the **fixed saved learner**; lower is better,
and zero is unexploitable. This is not self-play win rate or game points.
No renderer or sampled post-training match is involved.

By default, evaluations occur roughly every 1,048,576 agent steps (rounded up
to a complete rollout) and on the final checkpoint. With frozen self-play,
the initial checkpoint is also measured. `base.checkpoint_interval=-1` selects
this step cadence; `env.exploitability_interval_steps` changes it. Explicit
positive checkpoint intervals remain epoch-based; zero means final-only
(plus the frozen pool's initial checkpoint). Evaluations briefly pause training
and are included in elapsed time/SPS. The displayed values refer to the most
recent saved checkpoint, not every optimizer update.

`eval latest` now prints exact exploitability and exits, without a window.
`eval latest --render` explicitly requests the old viewer. Latest means the
newest nonempty `.bin` under `base.checkpoint_dir/goofspiel`, using nanosecond
modification times. New checkpoints include `.bin.ini` settings; old ones fall
back to `base.log_dir/goofspiel/RUN.ini`. Evaluation recovers the recorded model
shape and game settings automatically, including a Transformer checkpoint
when the current config says PufferNet. Explicit CLI settings still override
the recorded settings. The standalone `exploit`/`exploit_gpu` also accept
`latest`; CPU `exploit` remains PufferNet-only.

Ordinary `sweep` minimizes best exact exploitability and searches **34 active
dimensions per model**, including runtime/model size, PPO/Muon, schedules,
V-trace, frozen self-play and exact-response opponents. Both models use the
single [normal config](../../config/goofspiel.ini): **500 completed trials per
model, up to 50 million agent steps per trial**, with agents ranging from 4,096 to
131,072. Only the architecture selector changes between campaigns:

```bash
./puffer sweep
./puffer sweep --policy.architecture=transformer
```

Run these sequentially on one GPU. Game rules, seeds, exact-evaluation cadence
and objective stay fixed. The sampler resolves coupled shapes before launch.
Candidates exceeding the memory or graph-work limits are skipped without using
completed trials or worker failures. This estimate is not a guarantee of GPU
memory fit. Budgets round down to complete rollouts (49,938,432 steps for the
starting 16,384-agent/horizon-eight layout). No sweep guarantees zero exploitability.

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
calculation or PPO/optimizer implementation changed. The population wrapper
explicitly uses `--headless`.

The GPU environment now handles the renderer's host-only agent-count probe
without trying to free it as device memory. The GPU parity suite reproduces
that init/close lifecycle before every vector creation. Its optional hidden
Raylib window also verifies unchanged device environment bytes, a 980×276
screenshot and clean window shutdown:

```bash
ocean/goofspiel/build/test_gpu build/goofspiel-render.png
```

Run after `make ... test-gpu`, with a working display. The screenshot was
visually reviewed under WSL/Mesa. This is environment-renderer/lifecycle
evidence, not full native interactive-viewer or BF16 qualification.

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
`best_exploitability` (minimum across checkpoints saved in this run). The latter
is the Goofspiel default for training and sweeps; ordinary `score` is still game
points, not exact exploitability. Smaller is better. The checkpoint hook measures the metric
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
For standalone evaluation use `exploit_gpu latest` or the native `eval latest`.

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

## Native Transformer comparison

`policy.architecture=puffernet` remains the default, unchanged PufferNet
(linear encoder, stacked MinGRU/highway blocks, action/value decoder).
`policy.architecture=transformer` selects the experimental C++/CUDA/cuBLAS
Transformer. Both use the same native GPU sampler, PPO losses, Muon optimizer,
async actor snapshots, frozen-checkpoint pool and checkpoint machinery.
**The default build runs the environment on the CPU and the policy on the GPU.**
Do not add `--cu` for the CPU-simulator comparison. No Python is needed.

The Transformer requires compact, perfect-information, two-player egocentric
observations. Its four card-rank tokens encode normalized rank, both hand
availability flags, remaining-prize status and the current-prize flag. A fifth,
global token contains every remaining compact observation field. One shared
21-to-width linear projection embeds those features. Each observation has
independent noncausal attention; rollout time is folded into batch, **not**
attended across. The final global token feeds the standard action/value head.
There is no recurrent memory, temporal context, padding, dropout or FlashAttention.

The initial comparison uses width 32, two layers, two attention heads and a
4x GELU feed-forward expansion. This is **26,304 total parameters** versus
**7,840** for the existing width-32/two-layer PufferNet. It is not a
parameter-matched experiment. Widths must be multiples of eight in 8–256,
depth must be 1–16, and `policy.transformer_heads` must divide width.
Normalization/bias vectors use the existing optimizer's rank-1 update path;
only matrices receive Muon's orthogonalization. FP32 core gradients are cast
into the trainer's precision when BF16 is used.

```bash
# Separate output: does not replace ./puffer or change config/goofspiel.ini.
bash build.sh goofspiel build/puffer_goofspiel_transformer --float
make -C ocean/goofspiel exploit-gpu NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=sm_61
bash tests/build_goofspiel_transformer_tests.sh
./build/test_goofspiel_transformer

# Each destination must be new. Run sequentially on an available local GPU.
bash tests/run_goofspiel_transformer_smoke.sh reports/goofspiel_transformer/new_smoke
bash tests/run_goofspiel_transformer_pilot.sh reports/goofspiel_transformer/new_pilot
```

Select `CUDA_ARCH` for the GPU; `sm_61` and `--float` above match the local
GTX 1060. The pilot runs both policies for 262,144 total agent steps at seeds
73/74/75, with CPU simulation, GPU training, async and CUDA graphs enabled,
512 agents, two buffers, four CPU threads, horizon eight, minibatches of 512,
replay ratio four and four frozen opponent banks. Both policies share these
settings and the existing Goofspiel learning-rate/loss settings. Pool refresh
timeout is 65,536 steps. Five checkpoints per run are evaluated offline by exact
best response, so evaluator time is excluded from trainer time. A smoke/pilot
is not a sweep, a convergence claim, or evidence of an attention advantage.

Checkpoint tensors remain flat FP32 master weights; keep the logged INI and
use the matching architecture, width, depth and head count when loading.
Weights-only reload does not restore optimizer state. Wrong-sized Goofspiel
checkpoints are rejected instead of silently truncating a Transformer into
PufferNet. Frozen policies currently share the learner's architecture/head
setting; mixed PufferNet/Transformer banks or head-to-head matches are not
implemented. Separate exact-exploitability evaluation does not require them.

The CUDA exact evaluator accepts Transformer checkpoints with
`--policy.architecture=transformer --policy.hidden_size=32 --policy.num_layers=2
--policy.transformer_heads=2`. Its inference adapter shares token features and
the tested backbone, while exact tree enumeration and legal-card masking stay
unchanged. Inference is chunked to bound memory. The standalone **CPU** neural
evaluator/viewer remains PufferNet-only. Native `eval latest` supports both
architectures and prints exact exploitability headlessly; `--render` explicitly
opens the viewer against the CPU simulator.

Qualification: the adapter's 84 sampled parameter derivatives pass finite
differences; rollout/train outputs, CUDA-graph gradients, snapshot isolation,
observation independence, exact-evaluator probabilities (including a partial
513-row batch), and the uniform-policy exact value pass. Sixteen 4,096-step
FP32 CPU-environment runs cover both architectures, sync/async, graphs off/on,
and mirror/four-frozen-bank play. Existing CPU tests and ASan/UBSan pass.
BF16 adapter compilation was checked for SM80, but **BF16 training is not yet
qualified**. CUDA memcheck could not instrument this local WSL/GTX 1060 setup;
do not count its unsupported-device/debugger errors as a passing memory check.
Nine negative checks reject unsupported observation modes, dimensions, unknown
architectures and loading a Transformer checkpoint as PufferNet. Weights-only
reload, native headless evaluation, and bounded exact-response-pool refresh
also pass. The guard checks can be rerun with
`bash tests/check_goofspiel_transformer_guards.sh NEW_REPORT_DIRECTORY TRANSFORMER_CHECKPOINT`.

Initial receipts and three-seed pilot:
[2026-10-13 report](../../reports/goofspiel_transformer/20261013/README.md).

### Optional organized sweep launcher

The optional launcher uses [config/goofspiel.ini](../../config/goofspiel.ini),
just like ordinary `train` and `sweep`; there are no separate sweep profiles.
It only selects architecture, organizes output and accepts CLI overrides.
PufferNet and Transformer run independent searches of 500 trials each, with
up to 50 million agent steps per trial. **Setup does not start the campaigns.** The
launcher and its tests are native C++/Bash; no Python is needed. Its default
executable is `./puffer`, built above with `--cu` for GPU simulation.

The first trial uses the existing Goofspiel learning settings as a control.
Other trials search:

| Setting | Range | Sampling scale |
|---|---:|---|
| Learning rate | 0.0005–0.02 | log |
| Entropy coefficient | 0.0005–0.05 | log |
| Discount `gamma` | 0.98–0.9999 | logit |
| GAE `lambda` | 0.8–0.999 | logit |
| Policy clipping | 0.005–0.2 | linear |
| Value clipping | 0.01–5 | linear |
| Value-loss coefficient | 0.1–4 | linear |
| Gradient clipping | 0.1–1.5 | linear |
| Muon momentum | 0.6–0.97 | logit |
| Agent rows | 4,096–131,072 | powers of two |
| Buffers | 1 | fixed: GPU backend requirement |
| CPU threads | 1 | fixed: no CPU simulation workers |
| Rollout horizon | 4–32 | powers of two |
| Minibatch steps | 2,048–8,192 | powers of two |
| Replay ratio | 2–8 | continuous linear |
| Hidden width | 16–128 | powers of two |
| Layers | 1–4 | integer |
| Async actor/learner | off/on | binary |
| PufferNet: reset state each horizon | off/on | binary |
| Transformer: attention heads | 1–8 | powers of two |

Seventeen further dimensions cover:

- LR and entropy cosine decay: each off/on, LR minimum ratio 0–0.25 and entropy
  minimum ratio 0–1.
- Reward clipping: 0.5–1 continuously.
- V-trace: off/on, rho clip 0.1–2 and c clip 0.1–1.
- Frozen self-play: off/on, 2–5 total policies, historical fraction 0.25–0.75 in
  compatible minibatch increments, pool capacity 8–64 (powers of two), and opponent refresh
  timeout 262,144–8,000,000 steps (log scale).
- Exact-response opponents: off/on, 1–4 banks, history capacity 1–256 (powers
  of two), and latest-response probability 0–1.

The architecture-specific no-op is pinned: attention heads for PufferNet,
recurrent-state reset for Transformer. That leaves 34 active dimensions for
each model. Conditional settings act only when their feature is enabled.
Starting values remain the normal 16,384-agent, width-32/two-layer control.
Game/observation rules, seeds, GPU count, CUDA graphs, objective, evaluation
cadence and training budget are fixed. No legacy optimizer/replay flags are added.

The first broad campaign completed 17 trials before interruption. Trial 14
reached best exact exploitability 0.262708566 (final 0.393492722) in about 105
wall seconds; trial 11 reached 0.343341 in about 112 seconds. Five slow trials
consumed about 92% of completed-trial wall time and finished at 0.7465–1.0.
They used 7–8 layers and 256–512-step minibatches with high replay. Several
late cheap trials made only 345–552 optimizer updates and finished near 0.77.
These observations motivate the narrower runtime bounds, a higher replay floor,
and searching below the successful trials' old 0.02 clipping boundary. They do
not establish independent causal effects for each of the 36 parameters.

Protein's minimization path now converts observations to larger-is-better utility
before maintaining top trials, Pareto centers and GP targets. Previously these
stages preferred larger exploitability, while only the last acquisition step
used the minimization direction. Regression checks compare minimizing a loss
with maximizing its negative; all 412 existing maximization regression values
match the pre-change baseline bit-for-bit.

From the repository root:

```bash
# Config/layout checks only: no GPU initialization or report directory creation.
bash ocean/goofspiel/sweep.sh puffernet reports/goofspiel_sweeps/puffernet_01 --dry-run
bash ocean/goofspiel/sweep.sh transformer reports/goofspiel_sweeps/transformer_01 --dry-run

# Start these yourself, sequentially on the same available GPU.
bash ocean/goofspiel/sweep.sh puffernet reports/goofspiel_sweeps/puffernet_01
bash ocean/goofspiel/sweep.sh transformer reports/goofspiel_sweeps/transformer_01

# Rank completed trials across both campaigns (lower exploitability is better).
bash ocean/goofspiel/sweep.sh summary reports/goofspiel_sweeps/puffernet_01 reports/goofspiel_sweeps/transformer_01
```

`GOOFSPIEL_TRAIN_BINARY` selects a different native Goofspiel executable; the
default is `puffer`, which supports both policies when built for Goofspiel.
`CUDA_VISIBLE_DEVICES` selects the GPU. The tiny CPU launcher builds itself
with `${CXX:-g++}` when needed; it does not build the CUDA trainer. The known
local training path is FP32. BF16 training still needs qualification before a
modern-GPU performance claim. A successful dry run checks configuration, **not
GPU memory capacity or CUDA execution**.

CLI overrides take precedence. Values set the starting trial; range bounds
control the search. Setting both bounds equal pins a dimension:

```bash
bash ocean/goofspiel/sweep.sh transformer reports/goofspiel_sweeps/transformer_02 --dry-run \
    --vec.total_agents=8192 --train.minibatch_size=2048 \
    --train.replay_ratio=2 --policy.hidden_size=64
```

The helper and native sweep use the same ranges. Fixed bounds follow explicit
CLI values, including a changed trial budget. The first control trial and
ordinary training retain explicit settings and are validated as supplied.
Subsequent proposals choose the nearest historical fraction that fits the
sampled minibatch rows. If none fits inside the fraction interval, agent count
increases to the next allowed power of two. All choices remain inside the
configured ranges. This conditional search is not uniform over legal layouts.
Frozen-self-play off also disables exact-response opponents; enabled exact
banks are capped by available frozen banks, and enabled V-trace clips satisfy
`c <= rho`. The executed settings are logged and normalized back into Protein,
so it learns from the configuration that actually ran. The old fixed
`step=0.125` fraction grid is no longer used. Explicitly incompatible fixed
settings still fail preflight; memory and graph-work limits remain enforced.

`sweep.memory_limit_mb=0` uses free GPU memory measured at campaign start;
a positive value caps that limit in MiB. The per-trial estimate includes
rollouts, model caches, frozen policies, optimizer, exact evaluation and
headroom. It deliberately rejects large combinations before allocating them,
but is approximate: it can reject feasible trials and cannot prevent every OOM,
especially if another process takes GPU memory during the campaign.

`sweep.max_train_graph_work=32768` also bounds optimizer minibatches per rollout
times layers, times horizon for recurrent PufferNet. This screens very large
unrolled CUDA training graphs independently of the tensor-memory estimate;
zero disables the work guard. The unfinished trial's work was 245,760, versus
24,480 for successful trial 11. WSL logged GPU virtual-address reservation
failures immediately after the unfinished trial started. This makes capture
resource pressure a strong suspect, but does not prove the complete crash cause.

Scoring uses `best_exploitability`, `goal=minimize`, `downsample=1`. Sampled
post-training evaluations are disabled so they cannot replace the exact
objective. The launcher preserves the normal roughly 1,048,576-step exact
evaluation cadence. Optional `--checkpoints=N` replaces it with a target of N
intervals across the trial budget. The cadence rounds up to whole rollouts for
each sampled agents/horizon layout. An explicit positive
`--base.checkpoint_interval=N` uses trainer epochs instead.
Keep the same budget, search ranges and evaluation cadence for both campaigns.
The trainer rounds non-aligned step budgets down to complete rollouts; dry-run
output reports the effective step count.

Sweep workers stop after at least 20M agent steps if their best exact exploitability
has not improved by more than 0.005 over 15M steps. Smaller improvements accumulate
against the last significant improvement. The check runs at exact evaluations;
the trainer saves a final checkpoint and exits after the following rollout.
All earlier checkpoints, including the best, remain available. Ordinary `train`
does not use this rule. Set `sweep.early_stop_patience_steps=0` to disable it;
`early_stop_min_steps` and `early_stop_min_delta` configure the grace period and
improvement threshold. Logs record `env/early_stopped` and actual steps. This is
a heuristic for stalled trials, not a proof that they could never recover.

Each launch requires a **new** report directory. It retains the resolved
`sweep.ini`, a shell-escaped `command.sh` audit receipt, `sweep.log`, per-trial
logged INIs under `logs/goofspiel/`, and weights under `checkpoints/goofspiel/`.
The summary reports best/final exploitability with nine significant digits,
trainer time, steps and the exact per-trial settings file. Trainer time
**includes periodic exact evaluation** but excludes first-time CUDA graph capture;
it is neither complete process wall time nor pure model throughput.
Empty/incomplete results are not ranked. Sweep output defaults to trial summaries
(`sweep.live=0`). Set `sweep.live=1` to include per-trial training dashboards.
The launcher records visible output in `sweep.log`, including failure diagnostics.

Ctrl-C/SIGTERM is forwarded to this launcher's trial process group. The default
worker-failure limit is 100; existing reports are never automatically
overwritten or deleted. `sweep.max_suggestion_cost` is a search-cost setting,
**not a timeout**. Stopped campaigns retain artifacts, but Protein search state
is not restored by a new launch. Repeat promising settings on fresh seeds before
treating a sweep winner as an architecture-level result.

CPU-only configuration, argv round-trip, directory/exit-status and signal
contracts (using a stub executable, **not** a real training sweep):

```bash
bash tests/check_goofspiel_sweep.sh
bash tests/check_goofspiel_automatic.sh

# Real short training runs and three-trial sweeps for each architecture:
bash tests/check_goofspiel_live.sh NEW_REPORT_DIRECTORY
```
