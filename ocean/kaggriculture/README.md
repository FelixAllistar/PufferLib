# Kaggriculture on PufferLib 5.0

## Active setup: direct PufferNet, Final-B-inspired rules

There is one active config: `config/kaggriculture.ini`. Build with
`bash build.sh kaggriculture`; `./puffer train` reads that config directly.
The implementation lives in `ocean/kaggriculture_direct/`, but there is no
second environment config or profile overlay. The old macro controller is
retained for archived tools/checkpoints and is not executed by the new policy.

Current contract: policy ABI 6 / observation version 4, 5,856 actor features
plus 256 privileged paired-critic features, 20 unit heads × 500 candidates and
10 market heads × 1,903 candidates. The encoder is one stock PufferNet linear
projection followed by upstream MinGRU. Separate nonlinear unit/market heads
and the compact same-state paired critic remain; there is **no Transformer**.
The actor never receives the appended opponent-private critic features.

The action vocabulary and rule-aware design follow
[M & M & P & Q's Final B](https://github.com/msdsm/kaggriculture-solution).
Actions are primitive moves, work, inventory operations and exact-quantity
market orders, not macro requests to a route executor. Farmer then workers
reserve seeds and tile tasks. Market slots reserve money and inventory.
Invalid boundary moves, unproductive care/fertilizer, conflicting tasks and
seed overspending are masked. Overflow/final cash-out SELL and final shed DROP
are forced with singleton masks: probability one, entropy zero and no policy
gradient. The same conditional masks are stored during rollout and reused by
PPO, teacher KL and evaluation. Masking is not a full forward simulator:
an otherwise supported primitive action can still do nothing in the game.

The baseline uses terminal +1/0/-1 WLD, gamma 1, paired zero-sum value,
lambda .97, PPO clip .2, entropy .0015 and frozen-teacher KL .2.
Both current-policy seats train. Historical league, scripted training
opponents, opponent action noise, replay resets, PBRS and auxiliary rewards
are off. `root_money` remains visible. Optional land/crop/animal bonuses
remain available for later experiments; adding them is no longer pure WLD.

The shared profile has a 500M-agent-step budget, horizon 720, 128 agent rows
and two full sequences per minibatch. A game has 719 action steps; Puffer's
BF16 GAE needs a horizon divisible by eight. The conservative vector size
also fits the requested 1024×3 architecture on the 16GB GPU in qualification.
Do not infer stronger play or production throughput from smoke tests.

### Fresh BC: six shapes, one configuration

`bc_grid.hidden_sizes = 256,512,1024` and `bc_grid.num_layers = 2,3`
describe the requested models. The runner launches all six with CLI shape
overrides; it does not write six INI files. Each has its own checkpoint,
training log and provenance receipt under
`saved/kaggriculture/direct_v6/h{hidden}_l{layers}/`.

```bash
python ocean/kaggriculture/run.py build-bc
uv run --no-project --with numpy python ocean/kaggriculture/run.py prepare-bc
python ocean/kaggriculture/run.py bc-grid --dry-run
python ocean/kaggriculture/run.py bc-grid
# After an interruption: verify/keep completed shapes and run only the rest.
python ocean/kaggriculture/run.py bc-grid --resume
```

`--resume` does not overwrite or retrain completed models. It checks their
checkpoint checksums, receipts, architecture, command, config, dataset and
trainer binary before starting any remaining shape. Incomplete/mismatched
checkpoint-receipt pairs stop the queue for inspection. New receipts also
snapshot `default.ini` (older receipts only recorded the main config).
An unfinished model starts fresh; this is queue resume, not optimizer/epoch
resume. `--dry-run` only prints commands; it does not verify saved files.

Each pending model first runs a short-lived CUDA check, whose output is kept
in that model's timestamped log. This reports the actual CUDA error without
changing the trainer or retaining a GPU context in the queue process. Check
CUDA separately with `python ocean/kaggriculture_direct/check_cuda.py`.
Successful `nvidia-smi` output alone is not a CUDA compute health check.

Preparation re-encodes the existing, parity-checked Majkel1337 replay cache
into primitive ABI-6 labels, not old macro labels. These are not demonstrations
from the winning Final B policy. Unsupported, masked and forced teacher
components are ignored. Original actions still advance the replay so state
parity is preserved. Episodes cannot cross training and validation splits.
The dataset is streamed to disk and published without replacing existing files.

Fresh BC runs for 30 epochs with Adam 1e-4, epsilon 1e-5 and global gradient
clip 5; only the best held-out-CE weights are saved. Every shape starts fresh.
The critic is frozen during actor BC. Subsequent teacher-guided PPO uses the
matching BC policy as its frozen reference, with the teacher's own recurrent
state. Changing `bc_grid.output_root` starts a new version without overwriting
models. The default selected policy is 256×2; selecting another is explicit:

```bash
python ocean/kaggriculture/run.py train --hidden 512 --layers 3
python ocean/kaggriculture/run.py eval --hidden 512 --layers 3
```

Evaluation starts fresh with no reset bank or opponent noise. Native eval and
match are supported. The old CPU/web/Kaggle macro exporters cannot load ABI 6;
the build rejects those paths rather than silently exporting the wrong policy.

### Deliberate adaptations and remaining gaps

This is not a byte-for-byte reproduction of Final B. Puffer's Muon optimizer
and clipped squared value loss remain. The critic uses compact same-state
summaries rather than a Transformer global token. Recurrent memory replaces
their explicit inferred-inventory token; there is no explicit opponent
inventory tracker. Forced sales are selected before market sampling, with
singleton masks, rather than repairing sampled orders afterward.

Fresh actor BC currently uses masked CE, not the public final-stage BC
entropy/reference-KL objective. `run.py critic` offers frozen-actor regression
on replay WLD returns; it is **not** the reference solution's fresh-self-play
critic fitting and is not silently run by `bc-grid`. The BC/PPO loop can be
repeated with newly prepared demonstrations, but public-replay downloading,
heuristic refinement, fresh-self-play critic selection and CPU submission
export are separate work. No final-day search controller is used.

Teacher KL currently requires one live policy and one chronological pass
per rollout (`num_policies=1`, `replay_ratio=1`); the trainer checks this.
Joint old log probabilities are FP32 so BF16 rounding does not create
spurious PPO ratios. Full optimizer/reference-state resume is not added:
loading a checkpoint starts a fresh run as in the existing Puffer trainer.

### Verification

```bash
make -C ocean/kaggriculture_direct test replay-bridge
uv run --no-project --with numpy --with pytest python -m pytest -q ocean/kaggriculture_direct/tests
bash ocean/kaggriculture_direct/build_tests.sh
# Explicit opt-in on an idle GPU:
./build/test_kaggriculture_direct_kernels 0
./build/test_kaggriculture_direct_kernels 1
```

The CUDA test checks CPU/GPU prefix parity, exact unchanged-policy ratio 1,
zero forced-action gradients, masked teacher-KL gradients against an independent
double-precision oracle and graph-enabled sampling. CPU tests cover catalogs,
private-observation isolation, spending/seeds, full games, real replay
conversion, held-out separation and the six-model launcher.

## Historical ABI-5 tooling (not the active configuration)

Everything below describes archived macro/entity experiments. Their commands,
profiles, dataset formats and checkpoint shapes are not compatible with the
active direct-action setup without an explicit legacy build.

## Frozen replay ridge potential (opt-in experiment)

`fit_potential.py` fits an independent economic evaluator, not the PPO critic
or BC actor. It accepts exact-version `1.32.7` archived games from all identities
and both seats, regardless of outcome. Every replay frame must match the native
simulator before a game's compact feature cache is accepted. Sources are the
daily top-episode archives, not an unbiased sample of all competition games.

The current BC initializer used 518 Majkel1337 games from September 17–19:
442 training and 76 held out. This was an identity/date subset, not a 500-game
format limit. Ridge does not require costly controller-specific action labels.

```sh
make -C ocean/kaggriculture potential-bridge
OPENBLAS_NUM_THREADS=1 uv run --no-project --with numpy \
    python ocean/kaggriculture/fit_potential.py build REPLAY_DIRECTORY \
    --lib ocean/kaggriculture/build/potential.so --output NEW_DATA_DIRECTORY \
    --limit 10000 --stride 12 --workers 8
OPENBLAS_NUM_THREADS=1 uv run --no-project --with numpy \
    python ocean/kaggriculture/fit_potential.py fit \
    --dataset NEW_DATA_DIRECTORY --output NEW_FIT_DIRECTORY --gamma 0.999989986
```

Features come from the same native `potential.h` in data preparation and live
rewards: cash, land, workers, maintenance, crops/animals by type and age,
inventory, prices, public opponent state, and time/support interactions.
There are 205 features; no fitted coefficients are hand-set as asset rewards.
One frame per 12 turns plus the last nonterminal frame is the default cache;
runtime shaping still evaluates every game transition. All frames are checked
for parity even when only some are retained for fitting.

`--workers` runs independent replay checks in separate CPU processes (default 1).
Selection, result order, splits and features are identical across worker counts.
The exact comparison still distinguishes missing keys, list lengths and types
such as `false`, `0` and `0.0`; expensive difference paths are built only on
mismatches. `cached` in the progress log counts already-validated reused games.
`--limit 0` selects every compatible game in the input archives. Cache keys
include the native library hash, so a changed simulator is revalidated; preserve
old caches/fits separately when changing that library. No archives are unpacked.

The target is discounted terminal **cash gain / starting money**, with none
of the alive/growth/quality terms. Both seats and all states from a game share
one split, and games with the same initial seed are grouped together. Whole
games receive equal fitting weight. Train-only normalization and validation
select ridge alpha; the untouched test split reports early/middle/late errors
against a cash/time baseline including their interaction. Splits are not
identity- or date-held-out; domain-shift performance requires separate checks.

Outputs are `potential.bin`, coefficients/normalization/validation/provenance
in `potential.json`, and a frozen copy of the exact dataset manifest. Cached
features allow refitting for another gamma without reparsing the replays.
Different target scaling uses `reward_money` at runtime. Reset-start episodes
receive the exact discounted starting-cash correction rather than pretending
every reset started with 3,000. Predictions are frozen for the entire run.

`env.potential_beta=0` (default) disables it. Positive beta adds
`reward_money * beta * (gamma * phi_next - phi_now)`. Only genuine terminal
states have zero potential; rollout boundaries do not. The environment uses
the existing optional configure hook to check model format, gamma agreement
and `reward_clip=0`. No shared trainer/PPO/optimizer source changes are needed.
The sum of shaping rewards is logged as `potential_reward` (undiscounted).

After rebuilding current 5.0, a **dry run** for the separate beta-only sweep is:

```sh
uv run --no-project python ocean/kaggriculture/run.py sweep --profile ridge \
    --binary ./puffer_cpu --dry-run \
    --env.potential_path=NEW_FIT_DIRECTORY/potential.bin \
    --vec.total_agents=256 --train.horizon=512 --train.minibatch_size=512 \
    --train.total_timesteps=10000000 --sweep.max_runs=30
```

This profile selects 256x2 actor-only BC, terminal cash, no extra bonuses and
no annealing/clipping. It freezes **every inherited sweep dimension** to its
effective configured value and searches only beta in [0,2]; trial zero has
beta=0. It does not overwrite the working environment/default INIs. Preserve
the current shaped baseline as a separate comparison. Check the printed full
command before removing `--dry-run`; fixed dimensions and the configure hook
require the current branch, not an older binary merely called `puffer_cpu`.

CPU tests cover C/Python prediction parity, malformed model rejection,
fresh/reset discounted reward cancellation, unchanged simulated transitions,
group splits, regression/export and the beta-only profile. GPU runtime and
matched end-to-end throughput checks remain required before installing the
candidate over the active trainer. Offline prediction error alone does not
demonstrate improved PPO decisions or game scores.

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

### Rebuilding indexed reset banks

The preserved standalone differential suite now shares `replay_native.py`
instead of carrying a second ctypes/action ABI. After compiling the rule
library below, run:

```sh
uv run --no-project --with kaggle-environments==1.32.7 \
    python ocean/kaggriculture/parity.py --lib ocean/kaggriculture/build/libkaggriculture.so
```

Scripted/randomized actions, animal care/fertilizer/unfed yield, crop lifetime,
locked-tile movement and scarcity pricing checks pass against that official
package. This core-level suite uses some nondefault game rules for coverage;
it does not mean the native training adapter supports those configuration keys.
`--replay FILE.json` checks an archived game strictly. The legacy `--bench`
is retained but unqualified as a matched throughput comparison.

The historical HTTP archive collector is available as
`refresh_daily_replays.py` (planning unless `--download` is passed), with
`refresh_reset_archives.py` providing date-range download-budget/disk-reserve
preflight checks and ZIP episode-count checks. Both keep archives compressed
under `ROOT/raw/SLUG/SLUG.zip`. The bounded helper downloads when invoked;
it has no dry-run flag. Use its `--help` before selecting a destination.
Seven mocked-network tests cover date selection, resume offsets, ZIP checks,
no-overwrite publication and preflight refusal. Live endpoint availability,
HTTP 416 completion and large transfers are not qualified here. Budget checks
trust the server's advertised size; they are not a hard streaming byte cap.
ZIP publication checks structure, not every member's CRC; replay parsing and
parity checks remain required. The alternative Kaggle-CLI collection path in
`prepare_bc_replays.py` remains available.

`build_replay_state_bank.py` replays indexed episodes through the canonical
`core.h`, checks every frame, and verifies each selected state's serialized
round-trip and next step. Build a standalone rule library and supply an index
from `index_replay_states.py`:

```sh
mkdir -p ocean/kaggriculture/build
cc -x c -O2 -shared -fPIC ocean/kaggriculture/core.h -lm \
    -o ocean/kaggriculture/build/libkaggriculture.so
uv run --no-project python ocean/kaggriculture/build_replay_state_bank.py REPLAYS.zip \
    --index SELECTED.tsv --output NEW_BANK.kgb --skip-incompatible-episodes
```

The bank, manifest and summary must not already exist. Publication is not an
atomic three-file transaction: an interrupted or failed build can leave partial
outputs; retain the previous bank and use a fresh output path. A late frame
mismatch discards all selected snapshots from that episode. An all-rejected
input can produce an empty bank; inspect `record_count` before configuring it
for training, whose loader requires a nonempty bank. Two native-generated
regression cases qualify serialization and late-mismatch rejection, not official
expert replay compatibility.

For multiple dated daily archives, `build_diverse_reset_bank.py` retains the
legacy eight temporal anchors plus up to four scenario anchors per game,
seed-group holdouts, whole-episode parity rejection and incremental shard reuse:

```sh
uv run --no-project python ocean/kaggriculture/build_diverse_reset_bank.py DAILY.zip \
    --output NEW_DIRECTORY --jobs 2 --holdout-date YYYY-MM-DD
uv run --no-project python ocean/kaggriculture/audit_diverse_reset_bank.py \
    --directory NEW_DIRECTORY --config config/kaggriculture.ini --min-full-states 1
```

Use actual `kaggriculture-episodes-YYYY-MM-DD.zip` names and an intentional
holdout boundary. The auditor checks native defaults (the trainer does not
apply old game-rule overrides), manifests, per-state hashes, deserialization,
nonterminal turns and split leakage, and writes `audit.json`. Empty auxiliary
banks are allowed; `full.kgb` must be nonempty. Ten tests cover selection,
reuse, merging and native-generated archive-to-bank-to-audit behavior.
The real builder/auditor CLI passes with three complete 720-frame generated
games covering train, holdout and future splits. One/two-worker banks and
manifests are byte-identical, including after same-input resume. Large official
replay corpora remain unqualified; generated games are not official parity data.
Separately, opt-in `tests/test_core.py -k official` checks two complete games
generated by official Kaggle 1.32.7 (starter agents, seeds 7/42) frame-for-frame
and feeds them through the indexed-bank CLI. All frames, terminal money and
three snapshot resumes per game match. Run with
`KAGGRICULTURE_OFFICIAL_PARITY=1` and that Kaggle package installed. This does
not cover expert play or validate historical archives with different rules.
Do not change archives in place: legacy resume metadata binds archive size,
library hash and selection settings, not archive content hashes. Use a fresh
output directory for changed sources. Bank/manifest pairs are separate renames,
not a transactional bundle; run the auditor before using completed outputs.

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

The five legacy data files alone do not preserve the relabeling inputs.
Both BC JSON manifests reference `multi_intent_sidecar` outside this directory:
legacy `qualification/critic_matrix_20260923/terminal_only_v1/terminal.intents.jsonl.gz`
and `qualification/bc_expansion_20260923/remote_run.CPV5Ma/comparison/expanded.intents.jsonl.gz`.
Both were present at 32,433,200 bytes on 2026-09-25; equal sizes do not establish
equal contents. Preserve these separately with checksums and record their new
locations without overwriting original provenance. Individual record `tape`
paths also reference legacy inputs; rebuilding labels requires those tapes or
their source replays. Existing `.bc` fitting does not imply those inputs have
been transferred. Do not remove the legacy install on that basis.

Verify transferred files with SHA256 against the source before training. Do not
copy the old binary or global `default.ini` onto the new clone. The checked-in
environment config already includes the current remote sweep settings.

At a safe transfer boundary, make a checksum inventory on the source (reading
the full datasets is substantial I/O; don't do this under an active sweep):

```sh
cd /workspace/PufferLib
mkdir -p build
find -L saved/kaggriculture data/kaggriculture -type f -print0 \
    | sort -z | xargs -0 sha256sum > build/kag-assets.sha256
```

Transfer both directories with `rsync -aL`, plus `build/kag-assets.sha256`, into
the fresh clone without overwriting existing assets. From the new clone root,
run `sha256sum -c build/kag-assets.sha256`. A plain symlink-preserving copy can
appear complete while still depending on the old `legacy` directory.

`league.json` references its pool with portable relative paths, but
`initial_opponents.txt` contains machine-specific absolute paths. Regenerate a
separate list after transfer:

```sh
uv run --no-project --with numpy python ocean/kaggriculture/run.py league-sample \
    --banks 4 --seed 708 --opponents saved/kaggriculture/local_opponents.txt
```

For a league run, explicitly set
`--selfplay.initial_opponents=saved/kaggriculture/local_opponents.txt`.
This is a fresh sample of the preserved strategy, not necessarily the identical
four-bank allocation from the old machine. Do not enable it silently for the
current sweep, which deliberately uses `initial_opponents=None`. Both asset
directories are Git-ignored; cloning alone never supplies them.

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
trainer flag. Both adapters now expose the read-only Raylib renderer described below.
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
Learner-only gathering is restored as of September 27: opponent transitions do
not train either the actor or critic. Frozen opponent weights remain frozen.
Minibatch *sequences* (`minibatch_size / horizon`) must divide the learner-row
count. The earlier all-row comparison remains available in Git history.
Binary rollout masks are losslessly bitpacked (248 bytes per row), then
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

Ten tests pass, covering collection/reuse, failed-download cleanup,
deduplication, split/identity handling, invalid
episodes, cache publication/reuse, and a freshly compiled native-core cache
roundtrip. The latter uses generated frames; it is an ABI/cache integration
test, not an independent official replay-parity qualification. No new official
replay corpus was downloaded or fully processed during this port.

The old factory's collection stage is available as an explicit option:

```sh
uv run --no-project --with kaggle python ocean/kaggriculture/prepare_bc_replays.py \
    --fetch-days=2 --probe-days=7 --exact-version=1.32.7 \
    --download-dir=data/kaggriculture/raw --output=build/kaggriculture/inventory_v2
```

This requires the Kaggle CLI's normal authentication. It queries one page of
up to 200 updated official daily datasets, examines dated refs newest-first,
and downloads at most `--probe-days` candidates until `--fetch-days` archives
contain compatible replay metadata. Mixed-version archives contribute only
matching games; checking the first JSON alone would be insufficient. Existing
ZIPs are reused, incomplete downloads are not published, and archives from
other owners are excluded. A shortage fails explicitly rather than broadening
the version filter. The inventory records refs, reuse and per-archive counts.

Downloads are never implicit. `--cache-limit` bounds full replay parsing, not
archive download size: even a small cache may require large ZIPs. Collection
tests use a mocked CLI; no fresh official archives were downloaded during this
port. CLI flags were checked against the installed client. The retired factory's
reward fitting and legacy training commands are not invoked; use the canonical
dataset builder and `run.py` fitting workflow instead. Multi-teacher factory
orchestration and submission-revision stability screening remain unported.

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
    ocean/kaggriculture/tests/test_reward_gpu.cu \
    raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lm \
    -o build/kaggriculture/test_reward_gpu
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

After moving hosts, the builder also accepts existing v3 dataset metadata as
`--manifest`. Use `--tape-root` to locate the copied tapes by basename without
editing their original provenance. For the preserved expert inputs:

```sh
uv run --no-project --with numpy python ocean/kaggriculture/build_entity_bc_dataset.py \
    --manifest=data/kaggriculture/terminal.json \
    --tape-root=data/kaggriculture/tapes \
    --lib=ocean/kaggriculture/build/kag_bc_replay.so \
    --teacher=Majkel1337 --profile=ocean/kaggriculture/profiles/terminal.ini \
    --output=build/kaggriculture/relabeled_terminal_v1.bc
```

Episode IDs and recorded source hashes must match the tapes. The old split is
preserved, but labels/returns are rebuilt from the current code and explicit
profile; old metadata is not a substitute for that profile. Original metadata
is untouched and the new metadata records the actual resolved tape paths.
A two-game native regression rebuilds an identical binary after relocation
and rejects a mismatched source hash. This is not full-corpus or GPU training
qualification.

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

## Submission export (local qualification)

The public-observation controller is ported under `submission/`. Build it with
`make -C ocean/kaggriculture submission-bridge`; it needs only the native rule
and policy headers, libc and libm, not the trainer, CUDA or Raylib. It currently
targets the standard 2/2 controller with 10 market slots, 16 hands and no land
purchase delay. The packaging tool validates that contract before export.

```sh
uv run --no-project --with pytest --with numpy python -m pytest -q \
    ocean/kaggriculture/submission/test_entity_export.py
```

Tests reconstruct both players' observations using only public fields and the
acting player's private inventory. Across two seeds and deterministic/stochastic
sampling, 2,876 game transitions (5,752 player decisions) match full native
state exactly for observations, sampled heads, prefix masks, primitive actions
and sampling RNG. Repeated observation reads preserve history; midgame startup
without history is rejected. These are native-generated snapshots and supplied
random logits, not official Kaggle runtime or learned-network qualification.

The preserved NumPy entity inference module passes H256/L2 FP32 comparison
against the actual native encoder/MinGRU/decoder: 32-step sequences with resets
at steps 0 and 16, two generated parameter sets, and CUDA graphs off/on. Both
the 1,979 outputs (including value) and recurrent state differ by at most
1.2e-7 in this qualification. Generated weights include nonzero biases; this
checks parameter layout and inference math, not a selected trained policy.

Build the existing GPU oracle with the same flags as the native offline build,
substituting `tests/test_gpu.cu` for `bc.cu`. The tested local FP32 command is:

```sh
/usr/local/cuda/bin/nvcc -O2 --threads 2 -arch=sm_61 -std=c++17 \
    -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include \
    -I/usr/local/cuda/include/cccl -DPUFFER_KAGGRICULTURE -DENV_NAME=kaggriculture \
    '-DPUFFER_ENV_NAME="kaggriculture"' \
    '-DENV_HEADER="ocean/kaggriculture/kaggriculture.cu"' -DPRECISION_FLOAT \
    -Xcompiler=-fopenmp -Xcompiler=-Wno-narrowing --diag-suppress=2361 \
    --diag-suppress=111 --diag-suppress=128 ocean/kaggriculture/tests/test_gpu.cu \
    raylib-5.5_linux_amd64/lib/libraylib.a -L/usr/local/cuda/lib64 \
    -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand -lm -lpthread -lomp5 \
    -lGL -o build/kag_export_gpu_test
KAGGRICULTURE_GPU_TEST_BINARY=build/kag_export_gpu_test \
    uv run --no-project --with pytest --with numpy python -m pytest -qs \
    ocean/kaggriculture/submission/test_entity_network_export.py
```

Set `KAG_EXPORT_CHECKPOINT` to an existing raw H256/L2 checkpoint to test it
instead of generated weights. GPU tests skip unless the oracle path is supplied;
they explicitly require FP32. The preserved seven-model league's champion,
`run_terminal_league_continue_300m_v1_0000000299335680` (SHA256 prefix
`691825ac9c6c`), also passes this comparison: maximum logit error 0.000106812
and recurrent-state error 0.000030518 across both inputs and graph modes.
This checks synthetic observation sequences, not official-match behavior or
action parity at near-tied logits. BF16 remains unqualified. The full-state
oracle is test-only and is never bundled.

Create an archive locally, using the saved config from the checkpoint's run:

```sh
uv run --no-project --with numpy python ocean/kaggriculture/submission/package.py \
    --checkpoint=checkpoints/kaggriculture/RUN/CHECKPOINT.bin \
    --config=logs/kaggriculture/RUN.start.ini \
    --sampling=deterministic --output=artifacts/candidate_deterministic.tar.gz
```

Use `--sampling=stochastic` and a distinct output name for the stochastic agent.
The packager does not infer checkpoint identity or submit anything to Kaggle.
It checks finite weights, exact architecture size and canonical controller
settings, compiles the bridge, and publishes a four-file archive without
overwriting an existing file. Metadata includes weight/library/source hashes,
sampling mode and the supplied config's hash. The caller must supply the actual
run config; a matching file size alone cannot establish checkpoint semantics.

Local tests cover both sampling variants, unpacking, loading/executing without
`__file__`, repeated-frame caching, metadata hashes, mismatch rejection and
overwrite protection. They use a small zero-weight model and native-generated
observations, not a learned policy or the official runner. Build on compatible
Linux x86-64: the local shared library's system ABI is not yet qualified in the
official Kaggle container. Competition-container validation and selected-model
evaluation are required before considering these archives competition-ready.

The local official Python package (`kaggle-environments==1.32.7`, Kaggriculture
source SHA256 `bc8a54879ef02c7ea64b8b333d6a976f0ea65c4949149d01f463f23bccee653e`)
does complete eight full 720-frame file-runner games: deterministic/stochastic
archives, seeds 7/42 and both seats versus pass. Both players finish `DONE`,
without agent errors. These use the small zero-weight test checkpoint, not a
champion; they establish loading/observation compatibility, not policy strength
or equivalence to whatever package the competition currently runs.

To test an unpacked archive in a uv environment containing that pinned package:

```sh
uv run --no-project python ocean/kaggriculture/submission/test_entity_kaggle.py \
    UNPACKED_ARCHIVE_DIRECTORY --runner
```

Omit `--runner` for direct calls using the package's source loader and per-action
timing. Both paths run both seats/seeds, verify packaged file hashes and record
the installed version and environment-source hash. Neither uploads anything or
changes the archive's `official_runtime_verified=false` metadata: local success
does not certify the hosted competition runtime.

## Reward-only behavior search (CMA-MAE)

`qd.py` is an environment-local coordinator using `ribs==0.12.0`, not a trainer
fork or a new PPO loss. Each candidate reloads the same actor-only BC weights;
it does not inherit an archive elite's weights. BC metadata/hash checks reject
the critic-pretrained initializer. The five auxiliary reward ranges are in
`profiles/qd.ini`. Money stays at 1, land reward and PBRS at 0, and reward clipping
at 0. LR, entropy, gamma, GAE, replay ratio, resets, architecture, agents, horizon
and minibatch are fixed from the supplied config. Prepare prints the actual fixed
settings and requires an explicit step budget. This is not a normal Protein sweep.

There are two separate archives: CMA-MAE's adaptive thresholds guide proposals;
the result archive preserves the highest evaluated money in each behavior cell.
The default grid has 8x8 cells, with axes:

- **Animal value share:** animal-product production valued at fixed base prices,
  divided by total production value. Purchased/resold products do not count.
  This is a ratio of panel-average values, not an average of per-game ratios.
- **First-animal delay:** average first successful placement time as a fraction
  of the full episode. Never placing an animal counts as 1 (same as the last
  turn); `animal_seen` separately reports the fraction of games with a placement.
  This measures placing an animal, not producing its first output.

Also record second/third-plot delays, ending plots, successful plants and animal
placements, money and win rate. For training reset starts, event times are relative
to the remaining episode and prior placements/owned plots have delay 0. Archive
evaluation always disables resets, uses the same frozen opponents and seeds,
and balances seats. Cell quality is actual final cash, never shaped return.
The old pass/basic-rules bots are not used as quality judges.

Build Kaggriculture with the new environment telemetry before preparing the
experiment. **Nothing is launched by prepare**, and no config defaults are edited:

```sh
# Selected 2026-09-30: 200M per trial, anneal_lr=0 and anneal_ent_coef=0
# in config/kaggriculture.ini; keep the remaining PPO settings fixed.
uv run ocean/kaggriculture/qd.py prepare --binary ./puffer \
    --config config/kaggriculture.ini --steps 200000000 \
    --bc saved/kaggriculture/initial_bc.bin \
    --opponents saved/kaggriculture/sweep_league_20260929/initial_opponents.txt \
    --output logs/kaggriculture/qd_rewards_200m_20260930
# Only run this after the current training job finishes:
uv run ocean/kaggriculture/qd.py run logs/kaggriculture/qd_rewards_200m_20260930 --gpus 0,1
```

Prepare snapshots the BC weights, opponent panel, config and reward ranges and
records input hashes, including the binary. Each GPU runs one ordinary native
train/evaluate process. CMA proposes batches of 10; the two workers drain the
batch, then update covariance. Training is not synchronized across GPUs. There
are 500 candidates by default. A reset-free BC evaluation runs once before any
candidate, both as a baseline and a check that telemetry is available.

The console shows one updating line per GPU (append-only when redirected):

```text
GPU0 #12 train 18.3M SPS=94000 root$=72000 reset$=off plants=96.2 animals=10.1 plots=2.01
GPU1 #13 eval2/4 ...
```

`plants` and `animals` are successful **placements per completed game**, not
production units or the number currently alive. Training numbers describe the
latest completed-game window; archive decisions use the separate evaluation
panel. `reset$=off` means no reset episodes in that window, not zero cash.
`KAG_QD_METRICS=1` opts the child process into untruncated JSON environment metrics;
ordinary runs do not print that stream. The coordinator still parses step/SPS
fields from the existing trainer dashboard. No core changes are needed.

Artifacts: `manifest.json`, `fixed.ini`, `baseline.json`, per-trial configs,
full native logs, compact metric JSONL, final checkpoints and panel results;
`elites.json` links each cell to its trial/checkpoint. `status.json` reports
coverage and best money. `state.pkl` preserves CMA state and outstanding proposals.
Resume with the same run command. Finished trials are reused; interrupted training
restarts from BC, while completed training awaiting evaluation is not retrained.
Ctrl-C/SIGTERM stops this coordinator's children only. One failed worker does not
stop its peers or become an elite; an entirely failed batch stops for inspection.
Only load your own `state.pkl` (pickle is not safe for untrusted files).

Archive entries are provisional single-training-seed results. Confirm promising
elites on new evaluation seeds before promotion; do not silently rotate opponents
mid-search. Changing inputs/binary requires a new prepared experiment.

CPU-only orchestration tests (fake subprocess workers, no GPU allocation):

```sh
uv run --no-project --with ribs==0.12.0 --with pytest python -m pytest -q \
    ocean/kaggriculture/tests/test_qd.py
```

## Read-only rendering

Native non-headless `eval`/`match` now draws both farms, workers, crops/animals,
inventories, markets and shops using the ported Raylib renderer. It opens a
window only when rendering is requested. CPU evaluation reads the current game;
GPU evaluation synchronizes and copies one game to host. No rendering state is
added to `Env`, and training/headless evaluation never call this path. The
environment cleanup closes the window. All changes stay under this environment.

The upstream evaluation loop draws once per rollout, not once per game turn.
Use a short `train.horizon` for smoother viewing; it changes evaluation batching,
not checkpoint weights. ESC exits. The old pause/single-step help was removed
because those controls are not implemented by this loop. This is a state viewer,
not a port of the legacy manual-play CLI or a standalone CPU entity-policy loader.

Explicit hidden-window tests require a working display/OpenGL context:

```sh
make -C ocean/kaggriculture render-test
make -C ocean/kaggriculture render-gpu-test \
    NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=sm_61
```

The fixture covers every crop/animal icon, verifies game-state bytes are
unchanged, exports a screenshot and checks window cleanup. Local CPU/GPU
screenshots match exactly and were visually reviewed. Locked-tile stripes are
clipped to their cells, fixing an old drawing overflow. Replay bridge and
optimized/sanitized reward regressions also pass. Those standalone tests now
link Raylib like the native trainer; the submission bridge remains independent
of Raylib because it includes only `policy.h`/`core.h`.

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
