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

### Committed shared-core audit (2026-09-25)

Compared committed `4039919df` with upstream 5.0
`6ffa5b10dbbbe4d1e8288367c7d9d3acd3bad4a2`, excluding working-tree
changes. Only two files under `src/` differ: `ocean.cu` (+17 lines) and
`pufferl.cu` (+237/-32). All other tracked upstream `src/` files are unchanged,
including `algo.cu`. This does not mean training behavior is unchanged:

| Change | Purpose and behavioral scope |
| --- | --- |
| Custom network registration | Select Kaggriculture, Retro and Shenaniguns3D environment networks; Kaggriculture also supplies a decoder. |
| Kaggriculture prefix sampling | Recompute legality after each selected action head; inactive heads use action zero without consuming RNG. Rules remain environment-owned, but the sampler has Kaggriculture-specific calls. CPU sampling uploads required state; GPU setup assigns player/policy rows. |
| Learner-only rollout gathering | Exclude frozen-opponent rows from PPO inputs, recurrent state storage and minibatch counts. This is a training-data correction, not merely a speed optimization. |
| Batched discrete sampling | Gather per-bank logits, then sample all rows together. Neural inference remains per bank. Intended to preserve actions, masks and row RNG while reducing launches. |
| Optional packed masks | Archive binary masks in bytes and unpack minibatches before PPO. Changes storage and adds kernels, not intended legality or loss semantics. |
| Configurable reward clamp | Replace hard-coded [-1, 1] clipping with `train.reward_clip`; default 1 preserves upstream clipping, zero disables it. Non-default settings change reward targets. |
| Training checkpoint initialization | Load learner weights before self-play initialization; optimizer and step count start fresh. This is not full training-state resume. |
| Initial frozen opponents | Load one explicit checkpoint per bank from a text file. This seeds opponents; it is not a restored PSRO/PFSP population manager. |
| Fixed sweep dimensions | Skip equal min/max ranges after checking they match the configured value; require at least one varying dimension. |

Outside `src/`, `build.sh` adds environment dependency/link registration for
WebNav, Retro, Shenaniguns3D and ARPG. `config/default.ini` adds the reward-clamp
and initial-opponent settings plus checkpoint documentation. Other non-ocean
changes are environment configs, assets, tools, tests, TUI and documentation.
No old custom optimizer or loss implementation is included in this diff.

The generic GPU setup callback was not part of this historical committed
audit. The user subsequently approved it and Pokémon observation binding,
and declined PFSP restoration. Counts are
source-diff evidence, not performance or learning-quality qualification.

### Approved GPU setup port (2026-09-25)

Commit `1acc12528` replaces the Kaggriculture-specific setup call with optional
`PUF_GPU_SETUP` / `puf_gpu_setup`, passing the mask buffer and policy layout.
Mask and player-assignment logic remains inside the environment. Kaggriculture
five-policy CPU/GPU adapter parity passes 23,040 transitions each with graphs
off/on. No loss or optimizer implementation changed.

Goofspiel uses that hook for legal-card masks and policy-grouped IO, sharing
CPU policy assignment. Its GPU test covers 32,768 game transitions, exact
observations/masks/rewards/terminals and final state/log/RNG equality, one/five
policies, non-default stream and vector recreation. CPU tests and ASan/UBSan
pass; FP32 GPU native training completes 4,096 steps with graphs off/on and
four frozen banks. Exact-response training orchestration remains unfinished.
Pokémon's approved observation binding is implemented as an optional decoder
callback (seven added lines in `src/algo.cu`). Both rollout and training paths
pass the current flattened observation, with no loss/optimizer changes.
Environment-local tests exercise both architecture entry points using fixed
hidden inputs: CPU/GPU output parity and 172 numerical derivatives pass across
four batches. A Goofspiel graph-training smoke verifies the null callback path.
Pokémon environment setup, recurrent training and reset integration remain
unfinished; the callback alone does not make the environment trainable.
Legacy continuous PFSP is explicitly excluded by the user; fixed saved
opponents and external evaluation remain, not PFSP retention/resampling.

Pokémon configuration/post-step hooks were additionally approved. Commit
`984b48050` adds twelve guarded lines: configure before train/eval setup and
complete CPU resets after the worker loop and initial reset, before upload.
Only Pokémon currently defines these hooks. Build registration links the
pinned engine and selects the semantic CPU viewer; encoder/decoder registration
uses the existing custom-network pattern. Native H16/L1 FP32 training passes
2,048 steps with graphs off/on, 16 rows/two workers, four-species forced cores
and legality auditing. CPU evaluation completes two random-policy games.
The config removes retired optimizer/PFSP keys and uses synchronous core
scheduling. Named expert banks fail explicitly until their loader is ported;
state-bank training qualification, checkpoint curriculum cursors and broader
native evaluation remain unfinished. No performance/learning-quality claim.

Pokémon reset-bank follow-up: collected/audited 1,024 valid snapshots from 128
seeded random-policy games, then completed a 2,048-step native H16/L1 run with
reset probability one, graphs and legality auditing enabled. All three reset
phases were observed. The saved weights reload and complete eight native
evaluation games. Fixed an environment-only configuration gap: evaluation now
disables reset-bank sampling as well as forced-core drafts, consistent with the
standalone evaluator. Evaluation succeeds with an intentionally missing bank
path and reports zero reset/core fractions. Adapter, input-contract and
fixed-team tests pass, including the new evaluation configuration regression.
This does not certify user banks, learning quality or curriculum checkpointing.

Pokémon checkpoint follow-up wires the existing optional metadata hook after
both native save sites (six guarded trainer lines, no weight-format changes).
Environment code writes snapshot-specific `.bin.ini` files plus parent
`config.ini`; tests ensure a later save does not replace an earlier cursor.
Four native saves preserve assigned counts 12/12/12/18, with 12 completed
drafts at the final snapshot. The semantic CPU evaluator loads that checkpoint
and completes four games; a 256-step native continuation from explicit cursor
18/12 succeeds. This resumes weights and the assignment schedule, not optimizer
state, in-flight games or recurrent memory. Named expert-bank loading remains.

Fixed-bank architecture prerequisite: training no longer overwrites explicit
`vec.hist_policy_hidden_size/num_layers` when `selfplay.initial_opponents` is
provided and `opp_timeout_steps=0`. Rotating/current-run opponents retain the
upstream learner-matching architecture. All 21 native checkpoint tests pass on
SM61 FP32, including H16/L2 learner updates against an immutable H32/L1 opponent,
graphs off/on, weight-loading/seed checks and async/sync regressions. This uses
the existing frozen-policy allocator/loader, not a new loss or population
algorithm. Pokémon named-team assignment/manifest integration remains pending.

Pokémon fixed named-roster integration now uses only environment changes:
manifest validation selects existing `selfplay.initial_opponents`, explicit
historical architecture and timeout zero; `MY_VEC_INIT` assigns banks and their
prescribed teams/leads before resets. The manifest names the ordered checkpoint
text file, validated against all bank paths. Old optimizer/league config keys
are no longer injected. Core tests pass, including one/four-worker deterministic
trajectories and native vector assignments. Two GPU integration cases pass:
H8/L1 learner versus two generated H16/L1 experts with distinct team bindings,
2,048 steps, graphs off/on, legality auditing and completed curriculum drafts;
expert files remain unchanged. This restores fixed opponents, not PFSP or the
legacy league-management/search scripts. Those scripts still need disposition.

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

### Ignored experiment dispositions

- `compare_fixed_continuations.py` and its test are historical executor0/obs1
  EMAg ablations. Their explicit ABI checks, magnet checkpoint and `emag_kl_coef`
  knobs target the retired trainer. Preserve in the verified ignored-source
  backup; do not restore custom losses to make this experiment executable.
- `sweep_macro_memory.py` and its test are a fixed legacy frozen-bank grid
  (`frozen_bank_pct=.75`, old league commands/EMAg sidecars). Native Protein is
  the active sweep path. The historical experiment is preserved, not relabeled
  as a compatible current sweep.
- `pokemon/qd_selfplay.py` imports the old `cma_qd` search and generation-panel
  machinery. The user explicitly deferred QD experiments; preserve its source
  without restoring that search/trainer integration as a conversion requirement.
- Goofspiel's `train_population.sh` includes exact-response refresh and EMAg;
  its evaluator wrapper invokes retired CLI/keys. Behavior analysis is ported,
  but native population training and exact-response refresh are still pending,
  not silently replaced by ordinary selfplay.
- Kaggriculture's ignored `parity.py` is now ported, sharing the canonical
  replay bridge rather than duplicated ABI definitions. All scripted/randomized,
  animal, crop-lifetime, locked-worker and market-hinge checks pass against
  official 1.32.7. Historical benchmark mode is not a qualified matched speed
  comparison. This does not establish arbitrary expert-replay compatibility.

The legacy `scripts/goofspiel_behavior.py` was ignored in the main checkout,
not contained in the archived `5c` ref. It is now explicitly ported from that
local file, with grouping/selection tests and an actual native report-to-CLI
smoke. This confirms why ignored source/workflow files must be inventoried
before replacing the main checkout; recovery Git refs alone are insufficient.

The subsequent ignored-source audit found 22 files under the source/workflow
roots (excluding build/vendor/reference/dependency trees). All 22 have a local
tar backup at `/home/felix/puffertank/ignored-source-preservation.RSzjfX/ignored-source.tar.gz`,
SHA256 `e8f079fa4c326882b650b9ebf310fb174ccd2008456aa9048fab77e4ef6ed90f`;
`tar --diff` against the original files passed. This is local recovery, not a
published or portable dependency. The Abyss snapshot fetcher and Retro palette
generator are now tracked and reproduce their checked-in outputs exactly.
The remaining ignored Kaggriculture reset-bank/continuation/legacy-executor
tools, Pokémon QD script and Goofspiel population wrappers still need explicit
dispositions. Excluded directories and non-source asset formats require their
own inventory; this audit is not a complete backup of the old checkout.

### Non-Git asset handoff

These assets are not supplied by cloning `5.0`. Keep source installations until
the destination files and their checksums are verified; never replace a user's
existing files with an unverified bulk copy. Use the environment-specific
instructions linked below rather than copying old binaries or whole configs.

| Asset | Canonical destination / source procedure | Current evidence |
| --- | --- | --- |
| Kaggriculture initializers and league | `saved/kaggriculture/`; [transfer and league-path instructions](ocean/kaggriculture/README.md#build-and-assets-on-a-new-vast-box) | Nine local models match saved provenance. Regenerate absolute opponent paths on each host. |
| Kaggriculture reset and BC data | `data/kaggriculture/{reset.kgb,shaped.bc,shaped.json,terminal.bc,terminal.json}` | Remote files still symlink into `legacy/`, totaling 7.1 GiB. Both JSON manifests are copied locally and hash-verified; bulk banks/datasets remain remote. Preserve external raw-intent sidecars and source tapes for relabeling too; see README. |
| Bomberman champion and its config | `checkpoints/bomberman/1790155830168/{0000000999948288.bin,config.ini}`; [champion instructions](ocean/bomberman/README.md#fixed-champion-cpu-simulator) | Both are present locally. Checkpoint SHA256 `4e9097812e2ccdce2bf1fb18881bda373541b7c1d42ec570022631c7b47ca87e`; paired config SHA256 `9d07e2ed74af2c66d68e72b1ef6b69a416b4cd3e4540b5650cac6ab0f13b2ddb`. |
| Retro cartridge | User-supplied `ocean/retro/roms/smb1_ntsc.nes`; [accepted identities](ocean/retro/README.md#local-assets-and-simulator-tests) | Local 40,976-byte cartridge has accepted SHA1 `33d23c2f2cfa4c9efec87f7bc1321ce3ce6c89bd`; SHA256 `0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea`. ROM bytes and generated ROM-derived code stay out of Git. |
| WebNav text model/tokenizer | Ignored `build/webnav/reference/`; pinned fetch/checksum procedure in [WebNav README](ocean/webnav/README.md) | Public reproducible download, not a required copy from the old checkout. Browser executable and system dependencies are separate prerequisites. |
| Pokémon state banks / trained models | Regenerate banks with [offline collection tools](ocean/pokemon/README.md), or transfer explicit selected compatible files with their provenance | Source data and pinned engine are in the port; this is not a ROM environment. Smoke-generated banks do not replace user-trained assets or prove native reset integration. |
| Abyss raw calibration recordings | Optional ignored `ocean/abyss/data/recorded/frames.jsonl`; [data provenance](ocean/abyss/README.md) | Runtime catalog/scenario data is tracked. Raw user recordings are needed only for reproducing calibration, not ordinary training. |

This is the identified runtime-asset handoff, not an exhaustive archive of all
historical checkpoints, replay collections or private captures. Those remain
preserved in the source installs pending final inventory. A read-only remote
process check confirmed sweep PID 2601722 and trial-38 child PID 2711504 live;
no remote data copy, rebuild, configuration edit or deployment was performed.

On 2026-09-25, sweep PID 2601722 and trial-38 child 2711504 were still live.
Only the two small BC JSON manifests were downloaded, with identical source
and destination SHA256: terminal `db2288e7f5a04d1703b58769a966432ededb56dcd6a3a551c7c77040e687bec8`,
shaped `9cf5e38f8eb155b50de546c2a4dfbb59d2b24900a388e482402622b0b078e2ae`.
Both describe Majkel1337, 442 training and 76 validation games, no reset
trajectories, and unverified submission revision. Their external raw-intent
sidecars exist, each 32,433,200 bytes; sidecar transfer/hash verification and
the source-tape inventory remain outstanding. Bulk transfer remains deferred
while the sweep runs. No remote config, binary or process was changed.

Subsequently, both raw-intent sidecars and all 518 referenced gzip tapes were
copied with a 4 MiB/s transfer limit into ignored local `data/kaggriculture/`.
The sidecar SHA256 values match the remote source:
`terminal.intents.jsonl.gz` = `c29ab9dc562ed186192258f8d516fc034b2bb3156c614be00461863cbfd4717b`,
`expanded.intents.jsonl.gz` = `e3e99765d384b17eab0b1d2431bd3c2f06b89777dfc6019e2cd92ce1f7712aad`.
The distinct hashes confirm they must not be deduplicated by size alone.
The tapes total 19,013,957 bytes. Source/destination sorted `filename SHA256`
lists have identical SHA256 `16816ba4ec0bea759c897f799c380b75ef3e2318f061dd098c1e8c305a5f0738`.
Every tape decompresses and its episode ID, source hash and declared complete
frame count match the copied metadata. Original metadata remains unmodified:
resolve each record's tape basename under local `data/kaggriculture/tapes/`.
The first expert episode (109848551) also passes canonical `build_game`,
including terminal replay validation and generation of 720 observation rows.
This is not yet full-corpus qualification or proof of expert action coverage;
an all-518 replay/relabel check was started separately. Bulk `.bc` and reset
bank transfer remains pending. No datasets, models or private tapes enter Git.

The environment-local BC builder now accepts preserved v3 dataset metadata
and `--tape-root` for relocated inputs. It preserves episode splits, checks
episode/source identity, records new resolved paths and leaves original
metadata untouched. Two native publication tests pass, including byte-identical
binary rebuilding after relocation and rejection of mismatched provenance;
the opt-in GPU fitting test was skipped. No shared-core changes are involved.

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

The combined legacy MiniWoB entry points, wires, validation headers and tests
are now published for source preservation. Original files match the main
checkout byte-for-byte; the new launcher uses the shared WebNav compiler lock
and existing kernel resource guard. Its shell syntax passes and an unconfined
invocation is rejected before compilation. Fresh combined code generation
remains blocked by the previously observed safe-memory limit; no new compiler
attempt or relaxed cap is claimed. This is intentionally an unfinished port,
not a reduced-task replacement or native-training qualification.

Historical HTTP daily-archive refresh and its date/budget wrapper are ported
inside Kaggriculture, without trainer dependencies. Seven mocked-network tests
verify date filtering, resumed download offsets, no-overwrite ZIP publication,
and download-budget/disk-reserve refusal before transfer. Live endpoints and
HTTP 416 completion remain unqualified; advertised-size preflight is not a
streaming hard cap. No live downloads or remote changes were performed.

The indexed Kaggriculture reset-bank builder is restored against the canonical
rule core. Native-generated replay tests verify header/serialization ABI,
snapshot restore, late-mismatch whole-episode rejection and overwrite refusal.
This is not new official-replay parity evidence. Multi-day diverse-bank building
and its auditor are now ported with ten unit/native-generated checks, including
seed splits, incremental merge, late mismatch rejection and payload corruption.
The auditor follows native default game rules and permits empty auxiliary banks,
not an empty training bank. The actual builder/auditor CLI processes three full
720-frame generated games into all three splits; one/two-worker outputs and
same-input resumes are byte-identical for banks and manifests. Official
large-corpus qualification remains. Legacy resume metadata lacks archive content hashes;
changed sources require a new output directory. Failed output bundles are not
atomically published and must not replace an existing known-good bank.

Official archived-replay check: local episodes `90956870` and `90951851`
(module 1.32.6, 720 frames each) both fail the first resumed transition:
official `market.prices.CARROT=36`, native `35`. A freshly compiled legacy
`kaggriculture_core.c` produces the identical mismatch. Thus this specific
failure predates the port; its underlying cause is not established here.
Strict mode aborts; skip-incompatible mode rejects both complete episodes,
publishing zero records rather than treating them as verified reset states.
Do not use that empty diagnostic bank for training. Logs/summaries and both
compiled cores are retained locally in `/tmp/kag-official-bank.eIUDsn`.

The two original replay assets were copied without overwriting into ignored
`data/kaggriculture/replays/` and hashes verified:

- `top_90956870.json`: `28e07c4ccd73a6770d8adf679dad29633106958ef97cac0d3328563786d685da`
- `top_90951851.json`: `4cf4d92563b96f3c0e001264b763137b398b7d87359b62a02406f5fd9eab7d2b`

They are preserved diagnostic fixtures, not newly approved training data.
The remote reset/BC dataset bundle remains untransferred; existence of the
local `data/kaggriculture` directory alone does not satisfy that asset gate.

Follow-up pricing qualification against installed `kaggle-environments==1.32.7`
(environment source SHA256
`bc8a54879ef02c7ea64b8b333d6a976f0ea65c4949149d01f463f23bccee653e`):
all nine default products at inventories 0 through 20,000 match native prices
exactly (180,009 comparisons). The official function returns 35 for carrot
inventory 9,999, unlike the preserved 1.32.6 replay's 36. This establishes
agreement for current default pricing, not whole-game official parity or
the exact historical reason for the archived replay mismatch. No rules changed.
The opt-in regression can be reproduced from the repository root:

```sh
KAGGRICULTURE_OFFICIAL_PARITY=1 uv run --no-project --with pytest \
    --with kaggle-environments==1.32.7 python -m pytest -qs \
    ocean/kaggriculture/tests/test_core.py -k official_market
```

The same opt-in suite with `-k official` also runs two complete 720-frame
starter-versus-starter games (seeds 7 and 42) in the installed official 1.32.7
environment and compares every public/private snapshot and terminal money
against the native core. Both match exactly. The real indexed-bank CLI then
consumes those official frames, checks all 720 frames again, and publishes
three restored/next-step-verified states at turns 0, 360 and 718 per game.
All three opt-in tests pass. This is bounded current-version official-game
parity, not expert-strategy coverage, large-archive qualification or evidence
that historical incompatible replays can safely be accepted.

### Published clean-clone qualification

The independent HTTPS clone was subsequently fast-forwarded to published
`9fa9bea99`. No development-tree sources, model weights or datasets were copied
into it. Additional qualification from that clone:

- `make test sanitize` passes for Abyss, Goofspiel, Puffer Survivors and
  Shenaniguns.
- Box3D was freshly cloned from its documented public repository and checked
  out at `c4a414fcfe612a704dcd06ce921348d441271fc7`. Its Release static library
  builds with samples/tests disabled. This exposed a doubled shell continuation
  in the ARPG instructions, now corrected.
- ARPG simulator, Frontier and Reach tests pass, each normally and under
  ASan/UBSan. Shenaniguns3D passes 9,216 transitions/72 paired episodes both
  normally and under sanitizers. The separately built Box3D library itself
  was not sanitizer-instrumented.
- TrianglePath reports two passes and two intentional legacy-reference skips.
- The selected replay-preparation, reset-bank, archive-refresh, BC-label,
  payoff and behavior-selection suites report 48 passes plus two subtests.
- Goofspiel standalone CPU/CUDA exact evaluators build from published source;
  all 13 generated-checkpoint/uniform/behavior tests pass on local SM61 FP32.

Afterward `git diff --exit-code` is clean; the only untracked root files are
the two native binaries built during the earlier qualification. Environment
build artifacts are ignored. CPU suite logs are retained alongside the clone.
This expands clean-clone evidence, but does not establish bare-OS provisioning,
all-environment native training, interactive rendering, BF16 or remote cutover.

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
  retention in a rotating PFSP pool is not equivalent and was explicitly
  declined by the user; it is no longer a conversion requirement.
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
