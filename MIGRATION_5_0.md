# One canonical PufferLib 5.0 fork

Status: **in progress**, not a completed conversion of every environment.

Kaggriculture BC label/dataset tooling now targets the canonical native 2/2
bridge: observed multi-request strategies, teacher-prefix masks, stateful
expert returns, immutable episode holdout and raw-intent sidecars are preserved.
Resolved native config metadata coexists with legacy dataset loading. Synthetic
publication and label regression tests cover the data contract. An opt-in SM61
FP32 H32/L1 smoke checks checkpoint reload, holdout evaluation and weight-update
boundaries for actor-only, critic-only and joint fitting. Official expert corpus
coverage and useful large-model retraining are not yet qualified. This
port changes only `ocean/kaggriculture` and documentation, not the shared trainer.

Kaggriculture's public-state submission controller now uses `policy.h` directly,
without retired trainer fields. Two seeds in deterministic/stochastic modes
qualify 2,876 transitions and both seats against full native state for exact
observations, actions, masks and RNG. NumPy inference additionally matches native
H256/L2 FP32 outputs and recurrent states within 1.2e-7 across two generated
weight sets, 32 steps, resets and graphs off/on. Official Kaggle execution,
selected learned-checkpoint qualification remain unfinished; the oracle is
test-only. Local archive packaging now validates explicit canonical config and
checkpoint shape, records hashes, supports deterministic/stochastic sampling and
refuses overwrite. Both variants load without `__file__` and return actions in
local native-snapshot tests. The installed official Python package 1.32.7 also
completes eight full file-runner games (both seats, seeds 7/42 and both sampling
modes) with the zero-weight fixture. This is local package compatibility, not
policy-quality evidence. Hosted competition container ABI/execution and selected
learned-policy competition behavior remain unproven. The preserved league
champion does pass native-versus-NumPy FP32 inference on synthetic observations:
max logit/state errors 0.000106812/0.000030518 with graphs off/on.

The canonical local worktree now has a downloaded copy of the remote saved-model
bundle: two initializers and seven league checkpoints, all matching recorded
SHA256 hashes and 4,328,800-byte H256/L2 sizes. No model is committed. A separate
`local_opponents.txt` resolves the sampled league into this worktree; the copied
remote opponent list and training defaults are preserved. The remote sweep was
confirmed active and not changed. Its 7.1 GiB data bundle still uses legacy
symlinks and has not been copied locally. New-box instructions now require
dereferenced transfer, checksum validation and local opponent-list regeneration.

Kaggriculture's CPU/GPU native evaluation renderer is restored in the environment.
Hidden-window tests exercise Raylib/OpenGL, export visually reviewed identical
CPU/GPU screenshots, verify unchanged game-state bytes and close the window.
The drawing callback adds no per-environment state or shared trainer changes.
Replay/reward regression builds now link Raylib; the submission controller does
not. Native rendering updates once per rollout; legacy manual-play controls and
standalone CPU entity-policy loading are still separate unfinished workflows.

Canonical repository: `https://github.com/FelixAllistar/PufferLib.git`.
Canonical development branch: `5.0`, published on GitHub. Further ports and
clean-clone qualification remain in progress; check branch HEAD for updates.

## Source of truth and preservation

- Finalized upstream base: `6ffa5b10dbbbe4d1e8288367c7d9d3acd3bad4a2`.
- Qualified Kaggriculture runtime before unification: `1c30e3c2f`.
- Custom environment/source inventory: `5c` at `036cf4251`.
- Old GitHub `5.0`: `c3e28db18`, the May 26 upstream experimental branch.
  Its 24 right-only commits are upstream-authored. Its history is retained as
  a merge parent without replacing the finalized September runtime with the
  experimental API/optimizer. This is not a merge of the old custom trainer.
- Local recovery refs: `archive/5c-before-unification-20260924` and
  `archive/5.0-before-unification-20260924`.
- The latest multi-intent merge is already an ancestor of the inventoried `5c`.
  The separate detached multi-intent checkout is not the development target.

Work happens in the existing converted worktree until the ports are verified.
The usual local `pufferlib` checkout remains on `5c` during this phase. Its
tracked/untracked assets must be preserved when switching it to `5.0`.
The running Vast sweep must not be stopped, rebuilt underneath, or silently
changed by this conversion. Remote synchronization happens at a safe boundary.

Follow `SKILL_ISSUES.md`: use the new environment/network interfaces and
existing build patterns, keep changes atomic, preserve simulator behavior,
and keep shared-core changes minimal. Do not restore the old custom
losses/optimizer/trainer merely to compile an old environment.

Pokémon offline state-bank qualification: a 128-game random-policy collection
produces 1,024 states across all three phases, with byte-identical repeated
collection. Audits check provenance and sampling caps; malformed input and
overwrite attempts are rejected. State restoration/adapter tests cover 1,112
completed games and pass ASan/UBSan. The port also fixes the legacy auditor's
allocation leak on failed provenance checks. These tests do not qualify
native trainer reset scheduling or archived-policy collection quality.

Pokémon data regeneration is now preserved and checked against Showdown
0.11.11 and the pinned engine. Validation reproduced 556 catalog variants and
the generated semantic/legality tables using the fingerprint-checked saved
audit. It caught and corrected an added newline in the pinned source JSON;
that snapshot now matches `5c` byte for byte. Core-deck unit tests check all
540,274 triples and identical one/four-worker trajectories, with direct
environment callback fixtures rather than native trainer integration.

## Completion gates

1. Every fork-owned environment and relevant modification has a disposition
   below, with no silent omission. Source copying alone is not a verified port.
2. Simulator tests, native builds, masks/observations/terminals, render/viewer
   paths, and required custom encoders are checked where applicable. GPU and
   learning tests are reported separately from compilation and CPU tests.
3. Required workflows (BC, replay preparation, export/submission, league,
   sweeps, evaluation, emulator/browser tooling) use the canonical runtime or
   have an explicit documented external dependency.
4. A clean clone from GitHub can build. Runtime assets have a reproducible
   transfer/setup procedure; no dependency on a hidden sibling checkout.
5. The usual local checkout and remote deployment instructions both point to
   this branch. Old refs can remain as recovery history, not active forks.

## Environment inventory

This inventory compares `5c` with its upstream merge base `8dc45a39f` and with
the converted runtime. Unmodified upstream environments stay upstream-owned;
their final 5.0 implementations must not be overwritten by older copies.

| Environment | Conversion status / remaining work |
| --- | --- |
| Kaggriculture | CPU/GPU 2/2 simulator, entity network, masks, reset bank, BC/critic and league workflows qualified previously. Current actor-only sweep config and new-box build/asset instructions are in Git; finish replay/data, exporter/submission and renderer workflow audit. |
| TrianglePath | CPU/GPU adapters and shared exact solver ported. CPU legacy traces/sanitizers and 12-case GPU differential suite pass. No renderer existed in the legacy header. |
| Bomberman | CPU simulator/curriculum, masks and play/watch viewer ported; CPU tests/sanitizers and FP32 CUDA-learner smoke pass. Legacy GPU simulator ported: 4,096 CPU/GPU match transitions pass, including non-default streams and graph replay; 2,048-step FP32 native training saves checkpoints with graphs off/on. GPU path retains legacy limitations: no masks, reverse curriculum or rendering; single-policy only. Archived/new learned-checkpoint viewer traces match over 1,024 steps/32 resets. Newer local approach-reward code/settings ported with CPU unit/sanitizer and GPU parity tests. League workflows remain. |
| Goofspiel | CPU simulator/masks, renderer and CPU/CUDA standalone exact evaluators ported and tested; 4-bank async training smoke passes. CUDA exact values match CPU within 5e-7 on generated and native H32/L2 checkpoints (2–4 cards). Offline response-pool save/load and deterministic reservoir continuation pass with a native checkpoint. GPU simulator adapter, trainer exact-response refresh/persistence and exploitability-driven sweeps remain. |
| Abyss | CPU simulator, generated scenario/collider data and calibration tools ported. Mechanics tests/sanitizers, standalone headless evaluation and async GPU-learner smoke pass. Legacy renderer was a no-op; external raw calibration captures remain user assets. |
| ARPG | Latest CPU simulator/world/save/viewer/art ported; mechanics, Frontier, Reach, viewer and native CPU-under-NVCC tests pass. Async FP32 training smoke passes. Legacy analytic GPU simulator ported to native vector API; economy/pet-task/reset/stream/graph/recreation tests and 1,024-step FP32 training smoke pass. GPU physics differs from CPU Box3D; trained transfer, GPU rendering and interactive visual qualification remain. |
| Pokemon | Engine/core/data, semantic CPU model, standalone evaluator and offline state-bank tools ported. Bridge/game, old/new model parity, adapter/input/fixed-team and state-bank tests pass. CUDA model source ported with isolated FP32 encoder/decoder forward parity and 172 numerical parameter/input derivative checks; recurrent training unqualified. Native decoder observation binding, setup, reset scheduling, league/experiment integration and visual qualification remain; generic training explicitly blocked. |
| Puffer Survivors | CPU/GPU adapters, shared simulator, config, play/watch viewer and art ported. CPU sanitizers, CUDA mechanics/stream/recreation checks, FP32 CPU/GPU async training (GPU graphs on/off), and viewer checkpoint inference pass. Interactive visual qualification and remote BF16 testing remain. |
| Retro | CPU emulator, compiled-ROM generator, practice/playback helpers and differential suites ported. Reference, compiled parity and strict ASan/UBSan tests pass. Emulator source has two local fixes for unaligned pixel access and uninitialized audio snapshot bytes. Full-screen CNN passes exact legacy CPU traces at both resolutions and scale-4 FP32 GPU outputs/gradients/recurrent resets/graph-input tests. Native build and 256×2 practice config ported; async FP32 training smoke tests pass with graphs off/on and checkpoint saves. Play/watch/inspector ported with an environment-local C inference boundary; legacy learned and new native checkpoints pass headless inspection. Practice capture tool reproduces the 823-frame tape byte-for-byte from its original parent. Standalone speed panel ported; seeded one/four-worker reports match exactly on a learned checkpoint. Interactive graphics and automatic speed sweep scoring remain unqualified/unfinished. BF16 unqualified. User ROM stays ignored/local. |
| RetroArch | Restored documentation alias to Retro; `5c` has no independent implementation. |
| Shenaniguns | CPU simulator, native config, generic viewer and playable demo ported. Adapter tests/sanitizers and 2,048-step async GPU-learner smoke pass. Uses the standard upstream network; deployment-game parity remains a separate external test. |
| Shenaniguns3D | CPU simulator and formerly external character-controller source ported with pinned Box3D dependency. Paired seeded tests pass 9,216 transitions/72 resets across all modes/difficulties, including ASan/UBSan on environment/controller code. Custom sensor encoder ported; FP32 CPU/GPU encoder/recurrent forward and numerical encoder/decoder/MinGRU gradients pass. Native build/config ported; async 2,048-step graphs-on training and 1,024-step graphs-off checkpoint reload pass. Standalone viewer/evaluator ported with exact legacy deterministic traces; sensor, scripted-goal, randomized-course and evaluator sanitizer checks pass. Original GPU simulator/suite restored; bounded-drift baseline passes (max position difference 0.529, not exact physics parity). 5.0 GPU adapter ported; matched 4,096-step graphs-on/off runs save identical weights and report matching metrics. Interactive visuals, GPU rendering and BF16 remain unqualified/unimplemented. |
| WebNav | Pilot simulator/training/evaluator plus all five existing standalone families ported. Pilot training/browser/CPU trace checks pass. Family laws/native suites pass, with 968 fresh browser episodes and 38,000 public-scripted episodes across 38 tasks. Registry restored; all 130 pinned HTML fingerprints verified. Legacy training integration and workflow helpers pending. Obey family build/memory rules. |
| WebNav DOM | Existing public DOM-v2 projection/parser ported; browser contract suite passes. Frozen text encoder/cache has 1,120 exact reference cases. Public policy adapter and upstream CPU loader ported: 128-step H16/L2 legacy trace parity, sanitizer tests and 12 contract/weight rejection cases pass. Fresh combined simulator compilation exceeds local memory allowance; full training/GPU integration and production source-hash wiring remain pending. |
| Breakout | Fork delta is solely optional TUI frame capture; restored without altering upstream gameplay. Capture protocol tests and executable build pass; real GL/terminal rendering remains unqualified. |
| Robocode | Upstream simulator/config preserved; fork play/watch CLI and H radar toggle restored. CPU inference/bot/mirror/reset tests and sanitizers pass; interactive rendering remains unqualified. |

## Other source/workflow inventory

- Review fork changes in `build.sh`, `src/`, `pufferlib/`, `tests/`, `scripts/`
  and `tui/` by functionality; do not transplant the old shared runtime.
- Reconcile 32 modified/added configuration files. Old names such as frozen
  banks, GPU environment switches and action-mask sizes are not automatically
  valid in the finalized interface.
- Preserve relevant renderer assets in `resources/`, emulator source/patches,
  and task generators. Generated executables/profiling output and historical
  run logs are not replacements for reproducible builds or current configs.
- BC weights, replay/reset datasets, saved leagues, emulator ROMs, and other
  large/local assets need an explicit manifest and transfer procedure. Do not
  publish credentials or automatically add private/untracked assets to Git.
- Keep historical experiments/reports available in recovery history; classify
  replaced workflows rather than advertising them as working 5.0 features.

## Verification log

### Published clean-clone qualification

On 2026-09-24, a fresh shallow HTTPS clone of GitHub `5.0` at `72e8b9d4a`
was built in `/tmp/pufferlib-5.0-clean.wgS0ew/repo`. No source, Raylib, model,
or dataset was copied from the development checkout. Root `build.sh` fetched
Raylib 5.5 itself. Both native FP32 CPU-simulator/GPU-learner builds succeeded:

```sh
git clone --depth 1 --single-branch --branch 5.0 \
    https://github.com/FelixAllistar/PufferLib.git
cd PufferLib
CUDA_HOME=/usr/local/cuda NVCC_ARCH=sm_61 bash build.sh bomberman puffer_bomberman --float
CUDA_HOME=/usr/local/cuda NVCC_ARCH=sm_61 bash build.sh kaggriculture puffer_kaggriculture --float
```

Host dependencies were Ubuntu GCC 13.3, Clang 18.1.3, CUDA 12.6, ccache 4.9.1
and existing system graphics/NCCL/OpenMP development libraries. This is a
clean repository test, not a bare OS provisioning test. Select the target GPU
architecture and precision appropriately; SM61/FP32 is the local GTX 1060.

Bomberman `make test` passed. The compiled binary completed a fresh 2,048-step
async smoke (64 agents, two buffers/threads, H32/L1, horizon/minibatch 16,
32-tick games, no selfplay, `base.cudagraphs=-1`) and saved both checkpoints.
Kaggriculture's rule-regression and Python-compatible RNG tests passed in
optimized and ASan/UBSan builds: four tests, no skips. Reference-checkout parity
tests were deliberately not selected. Tracked source/config files remained
unchanged; only generated binaries appeared as untracked files.

This also verifies those paths build without the uncommitted generic GPU
setup hook. It does not qualify every environment on a clean clone, BF16,
Kaggriculture training without its external assets, or deployment on a new
Vast host. Those broader gates remain open. Local evidence logs are retained
beside the temporary clone; the remote sweep was not accessed or modified.

### Configuration and standalone evaluation audit

Kaggriculture replay preparation uses the canonical standalone `core.h`
through the preserved ctypes bridge. Exact-identity inventory, state index,
and primitive-tape cache tools are restored; ten tests include a freshly
compiled core/cache roundtrip, corrupted-terminal rejection, bounded collection
and failed-download cleanup. Explicit official daily-archive discovery/download
is integrated into the preparation tool; mocked CLI tests and installed CLI
help qualify the interface, not live authentication or new corpus availability.
Macro relabeling, dataset construction and local submission/export now have
separate qualification above. Multi-teacher orchestration and submission-revision
stability screening remain unported. Retired reward fitting and old trainer
commands are not automatically revived by collection.

The 32 fork-modified configuration paths have these dispositions:

- Native environment configs now exist for Abyss, ARPG, Bomberman, Goofspiel,
  Kaggriculture, Pokémon, Puffer Survivors, Retro, Shenaniguns3D, TrianglePath
  and WebNav. Presence is not full qualification: Pokémon training and the
  legacy WebNav training integration remain pending as recorded above.
- `config/default.ini` keeps upstream defaults except the previously ported
  initial-opponent manifest/reward-clamp settings and a weights-only loading
  comment. Old custom loss/optimizer defaults are not reinstated.
- `config/bomberman_league.ini` listed one champion. Its path is preserved in
  `ocean/bomberman/initial_opponents.txt`, with transfer/hash instructions in
  the environment README. Fixed-opponent training is qualified; permanent
  retention in a rotating PFSP pool is not equivalent and remains pending.
- `config/kaggriculture_clean.ini` and `config/kaggriculture_long.ini` are
  historical experiment presets containing legacy trainer keys and old
  checkpoint paths. Recover them from the archive ref for provenance; do not
  advertise them as native 5.0 launch configs or restore their optimizer knobs.
- `config/webnav_dom.ini` remains pending with the native DOM training port.
- All `config/kaggriculture.ini.bak*` and `config/retro.ini.bak` are historical
  snapshots retained in `archive/5c-before-unification-20260924`, not active
  configurations to copy over the finalized runtime.

`scripts/payoff_matrix.py` now invokes the environment-compiled native binary
with `match --headless`, evaluates both seat orientations, and emits CSV/JSON
payoffs and simple three-policy cycles. It does not train, promote, or update
a league. Example from the repository root:

```sh
uv run --no-project python scripts/payoff_matrix.py bomberman CHECKPOINT_DIRECTORY \
    --binary=./puffer_bomberman --games=256 --count=4 \
    --output=build/bomberman_payoffs
```

Use matching model architectures and the intended environment settings;
repeat `--override=section.key=value` as needed. Actual games per orientation
are recorded separately from requested games. Scores use native six-decimal
output; draw rates are dashboard-rounded to three decimals. Diagonal scores
are conventional 0.5, not measured self-matches. Output prefixes are replaced
if reused. Unit tests and a real two-checkpoint, two-seat, 64-total-game
Bomberman smoke pass; short-episode smoke scores are not quality evaluations.

The live main checkout audit found only the three dirty Bomberman files;
their simulator edits and reward defaults are preserved in `2f54dc508`.
The other migration gates and non-config workflow inventory remain open.

The standalone `scripts/export_onnx.py` and `scripts/verify_onnx.py` are ported
for standard discrete Linear/MinGRU/Linear policies. They do not export custom
entity/CNN/semantic networks or continuous log-standard-deviation heads.
Hidden size must be divisible by eight so native tensor alignment has no gaps;
wrong-size and partial-float weight files are rejected. Schema preprocessing
uses current environment headers, not the old hardcoded action counts.
Architecture dimensions come from config unless explicitly overridden: use
the checkpoint's recorded dimensions, not whatever a later sweep selected.

```sh
uv run --no-project --with onnx --with onnxruntime python scripts/export_onnx.py \
    bomberman --checkpoint=CHECKPOINT.bin --hidden-size=128 --num-layers=2 \
    --output=build/bomberman.onnx
uv run --no-project --with onnx --with onnxruntime python scripts/verify_onnx.py \
    build/bomberman.onnx --steps=32 --batch-size=3 --tolerance=0.0001
```

These commands also require the local Torch/NumPy environment (qualification:
Torch 2.13.0+cu126, ONNX 1.23.0, ONNX Runtime 1.30.0). The export is inference
only. Callers must zero recurrent state for terminated rows and apply the
environment's masks/sampler; the ONNX graph does neither automatically.
Synthetic 32-step recurrent math, including partial batch resets, agrees with
`src/puffercpu.c` within 1e-6. The learned Bomberman H128/L2 checkpoint exports
and passes 32-step dynamic-batch ONNX checking at 1e-4 absolute tolerance:
max logits 9.16e-5, value 1.24e-5, carry 5.92e-5. It does not pass 1e-5 absolute
on these random inputs. No native trainer or optimizer changes are involved.

- ARPG CPU port preserves the gameplay/world/render source and original art
  from `5c`; adapter changes place `obs_t` before the 5.0 Agent definition and
  update viewer inference/config calls. Box3D revision
  `c4a414fcfe612a704dcd06ce921348d441271fc7` is pinned in the environment README.
  Root `build.sh` adds five lines of ARPG dependency registration only. Native
  SM61 FP32 training completes 1,024 steps with async, two buffers, 16 agents
  and short episodes. CPU-under-NVCC mechanics and renderer compilation pass;
  camera/orders/pet-head/checkpoint tests run headlessly against the art files.

- 2026-09-24: inventory complete at the refs above. Twelve fork-only
  environments are absent from the converted tree. Kaggriculture and
  TrianglePath are partial/full subsystems, not proof of a complete fork port.
- Existing local worktrees are clean at inventory time. No local GPU training
  process was listed; verify availability again before GPU tests.
- Remote-only 500-trial actor-BC sweep preparation and checkpoint cleanup are
  recorded separately in `artifacts/kag_bc_sweep_20260924.vNn5z4` outside this
  checkout. Do not copy its modified global sweep defaults wholesale: other
  environments must retain their normal upstream architecture sweeps.
- Bomberman first port: simulator/constants blobs match `5c` exactly. Simulator
  and adapter tests pass with ASan/UBSan, including config loading, action-mask
  binding, terminal outputs and upstream match-score keys. Upstream standalone
  viewer builds and completes eight headless episodes both with random actions
  and the saved H128/L2 legacy champion (252,800 weights). This is load/execute
  compatibility, not proof of identical old/new inference or learning quality.
- Bomberman native FP32 trainer builds for local SM61 and completes a bounded
  16,384-step CPU-simulator / GPU-learner smoke with async enabled. Outputs are
  under `build/conversion/`, separate from all user runs. No shared source or
  build-script changes were needed for this CPU port.
- Fixed sweep bounds are now environment-local: three native tests pass,
  including two checkpoint-initialized trials with fixed architecture/budget.
  Seventeen Kaggriculture profile/league tests pass with the current actor-only
  config. No loss or optimizer changes were introduced.
- Abyss: simulator differs from `5c` only by observation typedef placement.
  Mechanics tests and ASan/UBSan pass; standalone evaluator completes four
  32-step episodes. Native FP32 build and 2,048-step async training smoke pass
  on SM61. Four-dimensional structural search is retained with fixed ranges.
  Existing data generators are preserved; a previously documented QSNA fetch
  script was absent in the source inventory and is explicitly not advertised.
- Shenaniguns: regression tests reproduced legacy reset erasure of final rewards
  and terminal flags. Fixed within the environment, also clearing stale rewards
  on timeout. Tests/ASan/UBSan pass, playable demo and generic viewer build,
  eight short headless episodes and a 2,048-step FP32 async training smoke pass.
  No shared-core edits. Interactive controls were built, not visually exercised.
- Puffer Survivors: replaced the legacy vector wrapper with the native GPU
  create/bind-stream/step API. CPU-under-NVCC now selects CPU state explicitly;
  the old compiler-detection macro incorrectly selected GPU state. Gameplay
  formulas and assets are preserved. Tests include 20,000 CPU steps, sanitizers,
  GPU reward/terminal semantics, non-default streams and vector recreation.
  Both trainers complete 2,048-step async FP32 runs; GPU simulation additionally
  passes CUDA graphs. The separate play/watch viewer uses upstream CPU inference
  and loads the new trainer checkpoint. No shared-core/build changes.
- TrianglePath GPU: native adapter and shared DP oracle pass 49,152 compared
  CPU/GPU transitions across all reward modes and edge-case cell ranges/heights.
  CPU optimized/sanitized legacy trace tests also pass. FP32 GPU trainer builds
  and completes 2,048 steps with async and CUDA graphs. No shared-core edits.
- Follow-up GPU logging audit found missing device `num_agents` metadata in
  the new Survivors/TrianglePath adapters: simulation/rewards worked but native
  environment metrics were blank. Both reset paths now initialize it; creation
  also finishes default-stream initialization before a non-default stream may
  use the vector. TrianglePath differential tests and trainer metric tests with
  graphs on/off pass. After local training stopped, Survivors CUDA parity and
  all four native trainer-metric regressions passed (both environments, graphs
  on/off). Tests are opt-in in `tests/test_gpu_env_metrics.py`.
- Bomberman play/watch now uses upstream masked CPU inference, including two
  separately loaded policies. Tests pass with the old H128/L2 champion and new
  trainer checkpoint: legal actions, joint/separate logits and recurrent resets.
  A four-game old-checkpoint headless mirror match completes. Saved-config
  lookup handles absolute paths and alternate checkpoint roots. No core edits;
  interactive graphics and exact old/new network parity remain unqualified.
