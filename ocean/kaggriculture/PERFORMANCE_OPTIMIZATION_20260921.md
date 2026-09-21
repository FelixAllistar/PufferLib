# Qualified 2/2 execution optimizations

This implements the first targets from `PERFORMANCE_REVIEW_20260921.md`.
The controller, encoder, action/observation ABI, rewards, job limits, automatic
chores, replay resets and number of opponents are unchanged. No old controller
was deleted and no checkpoint architecture was changed.

## Changes

- `src/pufferl.cu`: retain separate neural forwards and recurrent state for each
  opponent bank, gather their decoder rows into a preallocated buffer, then
  sample all rows together. Preserve row-to-match mapping and each row's Philox
  state. Gather/cast/mask archival remain in the captured graph. Scratch rows are
  disjoint per environment buffer; one-bank and continuous paths remain on the
  original route. Heterogeneous learner/opponent H/L remains supported.
- `multi_market.h` / `action_feasibility.h`: market preparation can consume the
  decoder's existing worker plan. Ordinary 2/2 actions now plan twice overall
  (sampler preview and execution), not three times. There is no persistent or
  cross-kernel worker-plan cache.
- `multi_executor.h`: compute each base intent's availability once and copy it
  to all five slots. No state changes occur between those base slots.
- `action_feasibility.h`: lazily cache immutable, unregionalized capacities
  once per sampled prefix. This shares the existing mode-3 capacity storage;
  worker/seed reservations and regional tests are still applied separately.
  A new mask state invalidates the cache every action. Decoder-only market
  preparation does not pay for this cache.
- `multi_executor.h`: use signed 16-bit fields for internal jobs, reducing the
  820-entry array from 19,680 to 9,840 bytes. Coordinates, operation/item IDs,
  request IDs and priorities fit these fields. Compile-time bounds/layout
  checks accompany the change. The job count, insertion order, loop order,
  signed sentinel values and assignment tie breaks are preserved.

The gathered BF16 logits buffer costs 8,105,984 bytes at 2,048 rows, or twice
that at 4,096 rows. It is allocated once, not once per timestep or graph.

## Matched end-to-end result

Hardware: RTX 5060 Ti, `sm_120`, BF16 GPU simulator, one GPU/buffer,
`base.async=0`, CUDA graphs enabled. Full H512/L3 learner and all eight H512/L3
opponent banks; 2,048 agents, horizon 720, minibatch 2,880, reset probability
0.8 with the full 78,864-record bank. Each variant performed three PPO updates
(4,423,680 transitions). The first update is excluded from steady-state figures.
Throughput is total transitions divided by elapsed time over updates 2–3,
not the arithmetic mean of the two reported SPS values.

| Variant | Steady SPS | Seconds/update | Model + sampling | Environment | PPO training |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original runtime | 15,317 | 96.268 | 51.415 s | 36.596 s | 8.188 s |
| Batched sampler + plan reuse + shared base capacities | 35,185 | 41.909 | 12.330 s | 21.301 s | 8.243 s |
| Above + prefix capacity cache + smaller job storage | 40,320 | 36.571 | 9.050 s | 19.255 s | 8.226 s |

The full patch is **2.632x faster end-to-end** in this matched short run; the
second stage adds **14.6% throughput** over the first. Unlike the earlier
fixed-logit benchmark, these measurements include neural inference, gathering
logits, simulation, PPO and normal loop overhead. This is not a claim about
long-run playing strength, every geometry, CPU simulation or asynchronous PPO.

These trials use a snapshot of the user's then-current remote config, with
explicit shared geometry/budget overrides. They do not reuse an older reward
preset. Evaluation was disabled equally for timing; no live config was edited.
The smaller-scratch build finished compiling during the original run's first
update; there was no competing GPU workload and no compilation during the
reported steady-state updates.

## Equivalence gates

- Native adapter, strict observation/controller contract, land-delay, reward,
  action-feasibility, episode-metrics, PLACE/shed and multi-executor tests pass
  for both stages.
- The same CPU driver compiled against original and candidate sources gives
  byte-identical base/prefix masks, sampled/decoded actions, RNG and resulting
  game states over 1,024 evenly spaced replay states for four turns each plus
  a fresh 720-turn episode. Both stages pass (4,816 paired game steps).
- GPU comparisons use the **actual `create_pufferl` / `rollout_start` /
  `pufferl_forward` path**, including neural forwards and gathered logits.
  They compare complete observations, actions, archived masks, log probabilities,
  values, rewards, terminals, recurrent states, discrete Philox counters/key/
  output and final native game states byte-for-byte. Struct padding and unused
  Gaussian RNG caches are deliberately not treated as discrete RNG state.
- The full patch passes ten configurations: graph capture/reuse, eager mode
  (`base.cudagraphs=-1`), one-bank fallback, fresh starts, raw 0/0, 2/1,
  1/0, 1/1, 2/0 and task 3/0. Each uses stochastic, carried recurrent,
  greedy and stochastic-again rollouts. Learner H64/L2 and opponent H32/L1
  exercise heterogeneous network layouts. The first stage passed the first
  six configurations; the second stage additionally covers all older modes.
- **All three trained checkpoints are byte-identical to the original run for
  both candidate stages.** The matched training entry calls the original
  `launch_train` function; it does not substitute a synthetic training loop.

Static CUDA resources (not a direct measurement of spill traffic):

| Kernel | Original registers / stack | First stage | Full patch |
| --- | ---: | ---: | ---: |
| `sample_logits` | 127 / 24,560 B | 127 / 24,544 B | 127 / 14,720 B |
| `kag_cuda_step_kernel` | 254 / 38,240 B | 199 / 34,944 B | 205 / 25,120 B |

## Reproduction and artifacts

- `tests/qualify_kag_performance.cu`: actual rollout byte dumps and PPO entry.
- `ocean/kaggriculture/tests/test_performance_replay.c`: CPU baseline/candidate
  replay-state comparison driver.
- `deploy_20260921/prepare_performance.sh BASELINE_SOURCE_ROOT`: run only in a
  separate candidate tree; preserves a config snapshot, checks CPU parity and
  builds original/candidate diagnostic executables without launching GPU work.
- `deploy_20260921/run_performance.sh QUALIFICATION_DIRECTORY [BASELINE_RESULTS]`:
  refuses GPU contention, bounds every run, compares rollout dumps/checkpoints.
  An optional prior results directory reuses the original baseline rather than
  rerunning it; absent regression cases are generated using its original binary.

Remote root: `/workspace/PufferLib-perf-20260921.Nk8MRa`.
Original/first-stage results: `qualification/performance.HsFBrA`.
Full-patch results: `qualification/compact-src/qualification/compact`.
Local timing logs: `/home/felix/puffertank/elite_replays/bc_multi_2026-09-21/qualification/performance_20260921`.
Temporary build-driver errors were fixed before these runs (missing C assert
include and const tensor shape access); failed attempts are retained separately.

## Production build, BC integration and installation

The normal `build.sh kaggriculture --gpu` launcher was built for `sm_120`.
The temporary selective source archive initially lacked shared Pokemon CPU
headers needed by the companion build; adding those tracked dependencies to
the temporary tree allowed `build.sh kaggriculture --fast` to finish. This did
not require a change to the normal build system or the real install's other games.

`smoke_performance_binary.sh` then loaded the existing H512/L3
`actor_100.bin` BC checkpoint into the normal production launcher and completed
two eMAG/PPO updates with all eight opponent banks and resets enabled. Both
policy and EMA checkpoints contain 4,025,560 finite float parameters. This uses
256 agents, horizon 16, minibatch 64 and 8,192 total transitions solely as an
integration test. Its SPS and inherited reset-state scores are not playing-
strength evidence or the full-size benchmark. Full-geometry eMAG VRAM headroom
was not qualified by this small test.

The qualified normal launcher, CPU companion and four modified runtime source
files are now installed in **`/workspace/PufferLib`**, not the temporary tree.
The previous executable/config/modified-source set is recoverable from:

`/workspace/PufferLib-backups/pre-performance.S3BRdU/previous-runtime.tar.gz`

Installed `puffer` SHA-256:
`51faaa3b629f31c104c55cd4b6dedbbd99e47591e9489151152c3683169e27a4`.
Installed CPU companion SHA-256:
`dbdb9fd4f180e7df05f06c24690682c3c77fa88719edebee60a964802b023dc3`.
Live config SHA-256 before and after installation:
`486201945171f7d0472c4db113337b003f5867a3527fb9808126e4b54e9fe259`.
Its H256/L2 setting, controller 2/2, reset probability 0.8 and reward settings
were preserved; H512/L3 was an isolated benchmark/smoke override, not a live
config change. The installed launcher's config check passes. No long training
run or sweep was started, and the GPU was left idle.

Production smoke: `qualification/compact-src/qualification/production_smoke.w9KDMT`.
Installation log: `qualification/compact-src/qualification/compact/activation.log`.
`activate_performance.sh` requires the exact old binary hash, completed parity
and smoke gates, and an idle GPU; it archives the previous runtime and atomically
replaces each selected file, with the training executable last. It does not
copy over the user's config, datasets, BC trainer or checkpoints.

## Remaining scope

No training hyperparameters, rewards or reset schedule were tuned. No dataset
was reparsed, BC checkpoint replaced, or BC source-fingerprint check bypassed.
Existing policy-v5 checkpoints retain the same architecture and controller
contract. Rebuilding BC tools changes their source fingerprint; requalification
of cached datasets should use the normal provenance path, not a forced bypass.

The planner is still serial per agent. Further candidates include measuring
smaller sampler thread blocks at fixed row count (currently 256 threads means
only eight blocks at 2,048 rows), and reducing planner complexity while
preserving assignment ordering. Neither is part of this qualified patch.
