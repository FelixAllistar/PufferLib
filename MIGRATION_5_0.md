# One canonical PufferLib 5.0 fork

Status: **in progress**, not a completed conversion of every environment.

Canonical repository: `https://github.com/FelixAllistar/PufferLib.git`.
Canonical development branch: `5.0`. Publication and clean-clone qualification
are still pending. Do not assume GitHub currently contains this working tree.

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
| Kaggriculture | CPU/GPU 2/2 simulator, entity network, masks, reset bank, BC/critic and league workflows qualified previously. Carry current actor-only sweep config into Git; finish replay/data, exporter/submission and renderer workflow audit. |
| TrianglePath | CPU port present; audit original GPU adapter, solver/viewer and tests for full coverage. |
| Bomberman | Missing from converted tree. Port latest simulator/curriculum, CPU/GPU adapters, masks, viewer and selfplay config. |
| Goofspiel | Missing. Port simulator/observations, CPU/GPU adapters, exact exploitability/opponent tooling and tests. |
| Abyss | Missing. Port simulator, generated scenario/collider assets, viewer and tests. |
| ARPG | Missing. Preserve latest source/assets; port adapter/build/viewer and test contracts. |
| Pokemon | Missing. Audit emulator dependencies, environment/model interfaces and personality/experiment tooling. |
| Puffer Survivors | Missing. Port current environment, controls/viewer, renderer/assets and tests. |
| Retro | Missing. Port emulator/practice/sweep tooling and full-screen CNN through the 5.0 network interfaces. Preserve ROMs locally; document external assets. |
| RetroArch | Missing. Audit standalone integration and dependencies; do not mark complete merely by copying its README. |
| Shenaniguns | Missing. Port environment and custom network/build integration. |
| Shenaniguns3D | Missing. Port environment and custom encoder, CPU/GPU paths and tests. |
| WebNav | Missing. Port native/browser/Bend workflows; obey its family build/memory rules. |
| WebNav DOM | Missing. Port environment and encoder against current WebNav contracts. |
| Breakout | Review the fork's one modified environment file against finalized upstream. |
| Robocode | Review the fork's two modified environment files against finalized upstream. |

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
