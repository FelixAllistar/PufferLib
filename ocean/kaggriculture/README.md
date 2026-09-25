# Kaggriculture on upstream PufferLib 5.0

This is the training port onto upstream revision `6ffa5b10d`, not the accumulated
5c trainer. It supports the current **2/2 controller, entity observation v3,
policy ABI 5**: 1,424 observations, 47 heads, 1,978 action logits and the entity
encoder with separate actor/value branches around upstream MinGRU.

## Current starting configuration

The canonical fork is `FelixAllistar/PufferLib`, branch `5.0`. The current
`config/kaggriculture.ini` is the actor-only BC sweep configuration, not the
earlier critic-initialized run described below: H256/L2, 30M nominal steps,
500 completed trials, no learning-rate or entropy annealing. Rewards, rollout,
minibatch, replay ratio and resource settings are searched. Model dimensions
and step budget use fixed sweep ranges; rebuild this branch before using them.
Other environments retain upstream's architecture and budget searches.

Use `./puffer_cpu sweep` after the build and asset transfer below. Do not use
`run.py --profile terminal` for this sweep: that explicitly selects the older
critic initializer and reward/gamma settings. Failed workers count as Protein
failure observations and are retried, up to 1,000 cumulative failures; a killed
parent process or hung worker is not automatically resumed. The final training
window's `root_money` is the search metric, not a held-out league evaluation.

The current config has no external initial opponents. Its four fixed opponent
banks start from the same BC initializer; the saved seven-member league is
available separately, not automatically selected by the sweep.

## Build and assets on a new Vast box

```bash
git clone --branch 5.0 https://github.com/FelixAllistar/PufferLib.git
cd PufferLib
CUDA_HOME=/usr/local/cuda NVCC_ARCH=sm_120 bash build.sh kaggriculture puffer_cpu
```

Use an image with CUDA (including `nvcc`), NCCL development files and the system
build dependencies listed by upstream. SM120 is for the current Blackwell
machines; select the actual GPU architecture on other machines. No training
starts during the build. A GPU simulator is a separate `--cu` build.

Weights and datasets are not supplied by cloning Git. Transfer the following
from the existing `/workspace/PufferLib` install, preserving relative paths:

- `saved/kaggriculture/`: actor/critic initializers, provenance JSON, league
  registry and its copied checkpoint pool. The active initializer is
  `initial_bc.bin` (4,328,800 bytes), SHA256
  `b114e8feded577dae233b4a036c15aaeb447bbb482c432cbb95b79f198703f88`.
- `data/kaggriculture/`: reset bank and offline datasets plus metadata.
  Dereference source symlinks during transfer (`rsync -aL`), since some still
  point into the preserved legacy install. `reset.kgb` is required for the
  configured resets; offline `.bc` files are only required for new BC fitting.

Verify transferred files with SHA256 against the source before training. Do not
copy the old binary or global `default.ini` onto the new clone. The checked-in
environment config already includes the current remote sweep settings.

```bash
ulimit -c 0
./puffer_cpu sweep
```

## Earlier qualified terminal-critic profile

On the prepared Vast install:

```bash
cd /workspace/PufferLib
./puffer train
```

The environment is compiled into the binary: do **not** append `kaggriculture`.
To rebuild: `NVCC_ARCH=sm_120 bash build.sh kaggriculture --cu`.
For a separate CPU-simulation/GPU-training binary, use
`NVCC_ARCH=sm_120 bash build.sh kaggriculture puffer_cpu`, then `./puffer_cpu train`.
Both use the same simulator/controller/rewards and CUDA inference/PPO. The CPU
adapter uploads game/policy snapshots for prefix-dependent GPU sampling.
`--cpu` is upstream's standalone play/eval build flag, not the CPU-simulation
trainer flag. Neither adapter currently ports the interactive renderer.
The explicit terminal profile loads `saved/kaggriculture/initial_bc_critic.bin`, uses terminal
cash gain only, a trainable critic, replay resets and frozen league opponents.
The 300M-step run is not automatically launched by installation.

Settings selected for the first upstream run:

| Setting | Value |
|---|---|
| Model | H256, 2 recurrent layers, 1,082,200 parameters |
| Initialization | Existing BC actor + terminal-return critic |
| Terminal reward | `4.71806717 * (ending_cash - episode_start_cash) / 3000` |
| Other rewards / clipping | All zero / disabled |
| Gamma | 0.999817491, matching critic targets |
| Replay resets | Probability 0.8; 78,864-state bank |
| Physical agent rows | 1,024: 640 learner, four banks of 96 opponents |
| Games | 512; 75% checkpoint games, 25% learner-mirror games |
| Rollout / minibatch | 256 steps / 8,192 transitions |
| RNN state | Carried across rollouts; reset on episode boundaries |
| PPO | Upstream losses, Muon, GAE, async pipeline; LR 0.0003 |

These are a qualified starting configuration, **not** an optimum transferred
from the old trainer. The horizon is shorter than the former 720; carrying
state preserves memory, not the full 720-step backpropagation window.
Upstream counts all physical agent rows in its step budget. At this league
split, 300M nominal steps contain approximately 187.5M learner transitions.
Minibatch *sequences* (`minibatch_size / horizon`) must divide the learner-row
count. Binary rollout masks are losslessly bitpacked (248 bytes per row), then
expanded for each minibatch before the unchanged upstream PPO kernels. Other
environments retain dense masks unless they explicitly opt into binary packing.
Discrete league banks retain separate inference, weights, recurrence and RNG;
their logits are gathered for one batched sampling launch per environment buffer.

`reset_fraction` measures completed logged episodes, not necessarily the
configured draw probability: continuations are shorter. `root_money` and
`reset_money` separate genuine starts and continuations. Production/action
metrics exclude activity inherited from bank states. During critic-only offline
fitting, the encoder and actor stay frozen while the value branch trains.
During PPO both actor and critic train normally.

For the earlier shaped-reward experiment with actor-only BC:

```bash
uv run --no-project python ocean/kaggriculture/run.py train --profile shaped
```

The shaped profile restores the earlier land/crop/animal high-water bonuses,
alive reward and dense quality reward. It does not enable PBRS. Terminal reward
units, observations and controller semantics are unchanged.

## Evaluation and checkpoint league

Use the runner for reset-free evaluation; native post-training evaluation is
disabled in the starting config so it cannot inherit training reset states.

```bash
uv run --no-project python ocean/kaggriculture/run.py eval \
    --base.load_model_path=checkpoints/kaggriculture/RUN/CHECKPOINT.bin \
    --env.bot_policy=1 --env.learner_seat=0
```

Repeat with seat 1 and bot 0 (pass). Bot 1 is the native reactive rules bot,
not a top competition agent. Evaluation is stochastic, as in upstream's sampler.
A direct native `match` needs two checkpoints, two agents per game and resets off;
the league runner sets these correctly and balances both seats.

The prepared league retains the seven compatible legacy models and the legacy
training mixture, explicitly tagged as such. Its weights are **not claimed to
be a newly evaluated upstream PSRO solution**. Four fixed banks approximate that
mixture by seeded systematic resampling. Opponent weights stay fixed within a
run; leave `selfplay.opp_timeout_steps=0`. Mid-game bank replacement is not part
of the qualified workflow.

All payoff collection, Nash solving and champion selection live in `run.py`,
outside PPO. To add a finished run, evaluate missing pairs, promote the empirical
champion in the registry and refresh the next run's banks:

```bash
uv run --no-project python ocean/kaggriculture/run.py league-add \
    --name candidate_1 --checkpoint checkpoints/kaggriculture/RUN/CHECKPOINT.bin
uv run --no-project --with numpy --with scipy python ocean/kaggriculture/run.py \
    league-eval --games 64 --bots
uv run --no-project --with numpy python ocean/kaggriculture/run.py \
    league-sample --banks 4 --seed 708
```

`league-eval` caches completed pair results, so an interrupted matrix resumes.
It updates the registry's champion automatically, not the actor initializer in
your training config. To continue that champion, explicitly override
`--base.load_model_path=PATH`. Small match counts are noisy; promotion is an
empirical ranking, not proof of stronger competition play. `league-add` accepts
only this qualified H256/L2 contract; file size alone does not establish the
semantics of an arbitrary imported checkpoint.

## Offline BC and critic fitting

The preserved expanded dataset contains **442 training + 76 held-out games**.
No replay parsing was repeated for this migration. The installed terminal and
shaped data links reuse the immutable original files; do not delete `legacy`.

```bash
uv run --no-project python ocean/kaggriculture/run.py build-bc --arch sm_120
uv run --no-project python ocean/kaggriculture/run.py bc \
    --bc.output=saved/kaggriculture/new_actor.bin
uv run --no-project python ocean/kaggriculture/run.py critic \
    --base.load_model_path=saved/kaggriculture/new_actor.bin \
    --bc.output=saved/kaggriculture/new_actor_critic.bin
# Or simultaneous actor CE + value regression:
uv run --no-project python ocean/kaggriculture/run.py bc-critic \
    --bc.output=saved/kaggriculture/new_joint.bin
```

The native offline application uses the actual upstream encoder/MinGRU/decoder
vtables. It does not instantiate a simulator or alter PPO. Actor loss is masked
conditional cross-entropy, normalized by labeled rows. Value targets are expert
Monte Carlo returns in PPO reward units; only the MSE loss is divided by
training-return variance. Critic-only fitting changes only the value branch.
Actor-only fitting leaves value-branch weights untouched, although shared
features can change its predictions. The immutable episode split is retained.

The standalone offline optimizer is Adam; it is not the old actor-BC SGD
optimizer and is **not** a replacement for upstream PPO's Muon. The selected
initializer remains the existing qualified checkpoint, not a newly retrained
one. Dataset metadata checks controller settings and, for value training,
reward units/gamma before launching. Outputs refuse overwrite and receive
provenance JSON. `bc.max_batches=0` uses all games; nonzero is a smoke-test limiter.

Replay inventory, state indexing and primitive-tape caching are now available
locally, independently of the trainer:

```sh
mkdir -p build/kaggriculture
cc -x c -O2 -shared -fPIC ocean/kaggriculture/core.h -lm \
    -o build/kaggriculture/replay_core.so
uv run --no-project python ocean/kaggriculture/prepare_bc_replays.py REPLAY_ZIP_DIRECTORY \
    --output=build/kaggriculture/inventory_v1
```

The first pass reads small metadata prefixes and reports exact display/agent
identities, game counts and an episode-level train/holdout split. It does not
infer leaderboard rank or merge similar-looking names. To cache a bounded
sample, use a new output directory and add `--teacher='EXACT DISPLAY NAME'`,
`--cache-limit=64` and `--lib=build/kaggriculture/replay_core.so`. Optionally
filter the exact submission name with `--agent-name`. The default replay
module filter remains `1.32.7`, not a claim about the latest upstream version.

Every newly cached tape must reproduce all supplied official frames and final
cash before publication. Cache keys include archive CRC and core-library hash;
reuse reruns the primitive actions and checks terminal cash. These tapes are
explicitly **not BC-ready**: they contain both primitive action streams, not
observations, macro labels, returns or serialized native states. Relabel them
for the chosen controller/reward contract before training. A state index is
likewise a reference into source replay data, not a resumable reset bank.

Eight tests pass, covering deduplication, split/identity handling, invalid
episodes, cache publication/reuse, and a freshly compiled native-core cache
roundtrip. The latter uses generated frames; it is an ABI/cache integration
test, not an independent official replay-parity qualification. No new official
replay corpus was downloaded or fully processed during this port.

Remote replay collection and Kaggle export still need migration from the legacy
install. Existing data/checkpoint loading does not qualify those workflows.

The environment now exposes `kag_apply_actions`: it executes both players'
primitive commands, advances policy observation history and computes the same
stateful rewards/logs as training, but leaves terminal state intact. The normal
macro `kag_step` decodes commands, calls it, and resets finished episodes as
before. Offline callers must supply projected macro history separately; this
entry point does not infer expert intent or make tapes BC-ready by itself.
The extraction matches the pre-change trace over 2,048 steps/64 terminal
transitions with one/two learning agents and terminal-only/shaped rewards.
Direct-primitive versus macro stepping passes optimized and sanitizer tests;
the host-under-NVCC test matches too. The independent GPU transition test
passes 2,048 CPU/GPU steps and 64 episode endings, alternating graph replay
and ordinary launches on a non-default stream. Native game states are
byte-identical; rewards, observations and logs agree within 2e-5 relative-plus-
absolute tolerance. It supplies fixed actions and does not qualify the sampler,
full trainer, BC projection quality or throughput.

```sh
nvcc -O2 -std=c++17 -arch=sm_61 -Isrc -Iraylib-5.5_linux_amd64/include \
    ocean/kaggriculture/tests/test_reward_gpu.cu -o build/kaggriculture/test_reward_gpu
./build/kaggriculture/test_reward_gpu
```

The CPU BC replay bridge is built with:

```sh
make -C ocean/kaggriculture replay-bridge replay-test
```

`ocean/kaggriculture/build/kag_bc_replay.so` exposes the current 2/2 float
observation/action ABI (1424 observations, 47 heads, 1978 mask bits, policy v5).
It uses the production decoder, prefix masks and shared primitive rewards.
Projected teacher heads update observation history, but the original primitive
action pair advances the game. Decode/work previews do not advance live state.
The source fingerprint is generated from the bridge, controller, rule core,
environment and config-interface sources. Reward/controller settings have a
separate semantics hash; replay seeds do not affect it. Gamma is reported
separately. It requires default native game rules and `train.reward_clip=0`,
and does not load reset banks or opponent networks.

Two 720-frame rule-bot replays pass exact state comparison, terminal-only cash
reward checks, mask/observation checks and ASan/UBSan. These are synthetic
integration fixtures, not proof of official expert label coverage.

Build a versioned v3 dataset from the prepared tape manifest with:

```sh
uv run --no-project --with numpy python ocean/kaggriculture/build_entity_bc_dataset.py \
    --manifest=build/kaggriculture/inventory_v1/summary.json \
    --lib=ocean/kaggriculture/build/kag_bc_replay.so \
    --teacher='EXACT DISPLAY NAME' \
    --profile=ocean/kaggriculture/profiles/terminal.ini \
    --output=build/kaggriculture/teacher_terminal_v1.bc
```

The builder preserves the manifest's episode-level holdout, projects observed
strategic actions onto the current 2/2 controller, and stores teacher-prefix
masks. Ambiguous or unrepresentable labels remain unsupervised rather than
being guessed. Original primitive actions are retained in the compressed
intent sidecar. Returns use the native stateful rewards and profile gamma;
terminal observations have no actor labels and NaN value targets.

The metadata records the fully resolved config and native source/semantics
fingerprints. Actor-only BC can reuse labels with different rewards/gamma;
critic or joint pretraining requires matching return settings. Both require
matching controller settings. Publication refuses existing output paths; the
three-file bundle is not transactionally atomic, so interrupted outputs must
not be used. This inherited builder holds the dataset and annotations in RAM:
qualify a bounded sample before processing a large corpus.

Label regressions and an end-to-end synthetic two-game publication test cover
packed masks, terminal rows, holdout layout, config validation and overwrite
rejection. The synthetic pass games do not establish real expert label coverage,
nonzero expert-return accuracy or policy quality.

The opt-in FP32 GPU smoke feeds the published dataset through `run.py` into the
native offline trainer at H32/L1. It saves an initial checkpoint, reloads it for
one update in each of actor-only, critic-only and joint modes, evaluates the
holdout and checks finite weights and provenance. Actor-only leaves the critic
branch byte-identical; critic-only leaves all other weights byte-identical;
joint changes both. Run it with:

```sh
uv run --no-project python ocean/kaggriculture/run.py build-bc \
    --arch sm_61 --precision fp32 --bc-binary build/kag_bc_fp32
KAG_BC_BINARY=build/kag_bc_fp32 uv run --no-project --with pytest --with numpy \
    python -m pytest -q ocean/kaggriculture/tests/test_dataset_publication.py
```

Choose the build architecture for your GPU. Without `KAG_BC_BINARY`, only the
CPU publication test runs. Official-corpus qualification, useful H256/L2
training and BF16 qualification remain pending for newly generated datasets.

## Shared-code boundary

Following `SKILL_ISSUES.md`, simulator, controller, observations, rewards,
resets, entity network, offline tools and league orchestration stay under
`ocean/kaggriculture`. Shared runtime changes are limited to:

- `src/ocean.cu`: seven lines of existing custom-network vtable dispatch.
- `src/pufferl.cu`: warm loading, optional reward clipping, prefix-sampler
  context, GPU policy-row hookup, initial opponent paths, and selecting only
  learner rows (including initial recurrent states) for PPO.

The row-selection fix is data routing: frozen-opponent behavior is not used as
learner experience. `src/algo.cu`, Muon, PPO/advantage/value losses, MinGRU
implementation, async scheduling and `src/pufferenv.h` remain upstream.
The sampler retains action/mask/log-prob/RNG parity. Market heads not reached
by the selected prefix use singleton masks and probability one in the stock loss.

No eMAG, QD, old trainer accumulation or optimizer/loss
experiments were imported. Raw/2/1 controllers, renderer,
opening curriculum and optional historical environments remain in the preserved
legacy trees; they are not silently interpreted as the new 2/2 configuration.

## Qualification

CPU checks (no GPU execution):

```bash
uv run --no-project --with pytest --with numpy --with scipy python -m pytest -q \
    ocean/kaggriculture/tests/test_core.py ocean/kaggriculture/tests/test_policy.py \
    ocean/kaggriculture/tests/test_profiles.py ocean/kaggriculture/tests/test_league.py
```

Optional legacy comparison uses `KAGGRICULTURE_REFERENCE_ROOT=/path/to/legacy`.
Optimized and ASan/UBSan traces cover rules, observations, decoded commands,
conditional masks and exact sampler RNG state. The standalone rule core also
passed interpreter parity against the installed Kaggle 1.32.7 rules.

GPU suites are opt-in: `test_gpu.py`, `test_network.py`, `test_bc.py`,
`tests/test_train_checkpoint.py` and `tests/test_train_policy_rows.py`. They
exercise real native kernels, graphs, recurrent carry, sync/async warm loading,
frozen-bank invariance, reset baselines, rewards, independent NumPy network
gradients and real checkpoint recurrence in FP32 and BF16. Tests leave no model
promoted. Complete scripts/logs live in the migration qualification artifacts;
tiny test throughput must not be presented as production training throughput.

Production-size qualification on Vast's RTX 5060 Ti: the 1,024-row async profile
completed 1,048,576 steps at about 9.4 GiB, with a measured steady interval near
15.9K physical steps/s. Total wall time was 143 seconds including graph capture;
the dashboard excludes capture time and its final async drain shows a misleading
SPS spike. A 2,048-row synchronous comparison held the model and horizon fixed
but reached only 13.7–13.8K steady SPS at about 10.3 GiB, so it was not selected.
These short runs qualify operation, not convergence or a speedup over legacy.

The preserved legacy champion also completed a reset-free, balanced-seat native
match against `terminal_final`: 28/32 wins, mean learner cash 91,106. This is a
checkpoint/match sanity check, not a newly selected champion or leaderboard result.
