# One canonical PufferLib 5.0 fork

Status: **in progress**, not a completed conversion of every environment.

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
| Bomberman | CPU simulator/curriculum, masks and play/watch viewer ported; CPU tests/sanitizers and FP32 CUDA-learner smoke pass. GPU simulator, league workflows and exact old/new checkpoint inference parity remain. |
| Goofspiel | CPU simulator/masks, renderer and standalone exact evaluator ported and tested; 4-bank async training smoke passes. GPU adapters/evaluator, exact-response refresh/persistence and exploitability-driven sweeps remain. |
| Abyss | CPU simulator, generated scenario/collider data and calibration tools ported. Mechanics tests/sanitizers, standalone headless evaluation and async GPU-learner smoke pass. Legacy renderer was a no-op; external raw calibration captures remain user assets. |
| ARPG | Missing. Preserve latest source/assets; port adapter/build/viewer and test contracts. |
| Pokemon | Missing. Audit emulator dependencies, environment/model interfaces and personality/experiment tooling. |
| Puffer Survivors | CPU/GPU adapters, shared simulator, config, play/watch viewer and art ported. CPU sanitizers, CUDA mechanics/stream/recreation checks, FP32 CPU/GPU async training (GPU graphs on/off), and viewer checkpoint inference pass. Interactive visual qualification and remote BF16 testing remain. |
| Retro | Missing. Port emulator/practice/sweep tooling and full-screen CNN through the 5.0 network interfaces. Preserve ROMs locally; document external assets. |
| RetroArch | Audit confirms `5c` contains only a README alias to Retro, not an independent implementation. Preserve the alias when the Retro build/workflow is ported. |
| Shenaniguns | CPU simulator, native config, generic viewer and playable demo ported. Adapter tests/sanitizers and 2,048-step async GPU-learner smoke pass. Uses the standard upstream network; deployment-game parity remains a separate external test. |
| Shenaniguns3D | Missing. Port environment and custom encoder, CPU/GPU paths and tests. |
| WebNav | Missing. Port native/browser/Bend workflows; obey its family build/memory rules. |
| WebNav DOM | Missing. Port environment and encoder against current WebNav contracts. |
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
