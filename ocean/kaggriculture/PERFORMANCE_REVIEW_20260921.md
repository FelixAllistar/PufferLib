# Kaggriculture 2/2 performance review

Follow-up: the proposed execution optimizations are now implemented and have
passed matched rollout/checkpoint tests. See
[PERFORMANCE_OPTIMIZATION_20260921.md](PERFORMANCE_OPTIMIZATION_20260921.md)
for the 2.632x end-to-end result and qualification scope. The measurements and
inspection notes below describe the original pre-optimization runtime.

Purpose: optimize execution while preserving the new controller's capabilities
and learning semantics. This is a diagnosis/handoff, not an optimization patch.
Do not shrink the policy, remove resets, simplify strategic decisions, or alter
rewards to claim that the execution regression is fixed.

## Verified measurements

The active remote `/workspace/PufferLib/puffer` is the CUDA simulator build for
RTX 5060 Ti (`sm_120`). The running executable and installed file both hash to
`8384724b9c3f7cb3424637cc09978ae8c7b14b412d2eed437d018132c33af18f`.
`cuobjdump` includes `kag_cuda_step_kernel` and `sample_logits`. The simulator,
prefix legality checks, action sampling, neural policy, and PPO math execute
on CUDA. CPU setup, logging, checkpoint I/O, and orchestration still exist.
The separate replay-BC trainer currently performs its momentum update on the
host; that is not the cause of the PPO rollout slowdown.

Same 2,048-agent / horizon-720 / H512-L3 geometry, second training update:

| Component | Old 2/1 sweep run 0 | New 2/2 integration check |
| --- | ---: | ---: |
| Model plus sampling | 2.923 s | 105.553 s |
| Environment | 17.201 s | 44.295 s |
| PPO training | 6.717 s | 8.200 s |
| Wall time | 26.854 s | 158.077 s |
| Steps/s | 54,911 | 9,328 |

Sources: old `logs/kaggriculture/sweep_1789940990348_0000.ini`, new
`qualification/production_20260921/pilot.5SAQq3/summary.json`. Archived and new
`rollout_start`/`rollout_finish` use the same CUDA-event boundaries for these
timers. Model time includes the sampler and its planner, not just the encoder.
Policies and state/action trajectories differ, so these are localization
evidence, not a controlled attribution of each code change.

The user's subsequent run `1789996751637` uses H256/L2, 4,096 agents, horizon
360, minibatch 1,440, eight H256/L2 frozen banks, and reset probability 0.8.
Its visible updates 2–4 showed about 15.6–16.2k SPS: roughly 66% model/sampler,
24% environment, and 8–9% training. Earlier startup snapshots confirm tests
with reset probability 0. No benchmark was launched alongside this active run.

## Concrete redundant work and GPU layout concerns

1. **Three worker plans on the ordinary network-controlled 2/2 path.**
   `src/pufferl.cu::sample_logits` calls
   `action_feasibility.h::kag_mask_prepare_market`, which calls
   `multi_executor.h::kag_multi_work` for worker-side shed transfers.
   `multi_market.h::kag_decode_multi_action` calls `kag_multi_work` for actual
   commands, then calls `kag_mask_prepare_market`, which runs it again.
   Thus the execution decoder alone computes the same worker plan twice.
   Reusing the already decoded work for its own market prefix is a narrow
   candidate; cross-kernel sampler-to-step reuse needs more careful lifecycle,
   policy/bot, reset, and action-identity validation. The sampler's `prepared`
   flag already avoids repeating this plan for every market head: do not
   describe it as ten planner calls per sampling invocation.

2. **Identical base capacities scanned five times.**
   `multi_executor.h::kag_multi_write_mask` loops over five slots and, within
   every slot, calls `kag_multi_capacity` for the same 28 intents against the
   same unchanged state. This is 140 capacity queries per mask, before the
   prefix sampler rescans capacities for intent, quantity and region choices.
   `kag_multi_capacity` scans tiles or workers. Cache immutable per-state
   capacities once and apply prefix-specific resource reservations separately;
   do not reuse stale results across state changes or mistake base capacity
   for prefix legality. Measure whether added cache storage increases spilling.

3. **Large serial per-thread planner scratch.**
   `KAG_MULTI_MAX_JOBS = 320 + 5*100 = 820`; each `KagMultiJob` contains six ints,
   so its local jobs array alone is 19,680 bytes. Assignment loops over assigned
   workers, remaining workers, and jobs; worst-case work scales approximately
   as workers squared times jobs. One sampler thread handles an entire agent;
   one environment thread handles an entire match.

   Static CUDA resource usage (bytes per thread):

   | Kernel | Old registers / stack | New registers / stack |
   | --- | ---: | ---: |
   | `sample_logits` | 95 / 4,240 | 127 / 24,560 |
   | `kag_cuda_step_kernel` | 200 / 15,488 | 254 / 38,240 |

   This motivates a local-memory/register-pressure investigation; static
   resource numbers are not measured spill traffic or proof of a specific
   occupancy bottleneck. Having unused total VRAM does not eliminate this cost.

4. **Small serialized frozen-bank samplers.**
   `src/pufferl.cu::pufferl_forward` launches a policy and sampler separately
   for the learner plus each of eight banks on the same stream. At 2,048 total
   rows and frozen fraction 0.75, the learner has 1,280 rows and each frozen bank
   has 96. The sampler uses `BLOCK_SIZE=256`, so each frozen launch is one partly
   occupied block. At 4,096 rows it is still only 192 rows per frozen bank.
   Consider batching sampling across banks after writing logits to a shared
   output, while preserving per-row RNG streams and controller metadata.

5. **Graphs exist; logical results are not automatically cached.**
   `base.cudagraphs=1` captures forward/sampling and training launch sequences.
   It does not memoize tile capacities or decoded worker assignments. PPO does
   store the actual sampled-prefix masks and reuses them during optimization;
   it does not rerun this planner to invent different training masks.

## CPU fallback is not a one-switch sampler fix

Without `PUFFER_GPU_ENV`, `pufferl_forward` copies a full host `Env` mirror to
the GPU each step for prefix-dependent sampling. The expensive sampler/planner
still runs on CUDA in that configuration, while simulation moves to CPU.
Measure both complete pipelines before deciding on CPU simulation; do not
compare only native game-core steps and omit masking, inference and transfers.
The old 2/1 GPU run's throughput is evidence that the GPU can serve this game
well, not proof that every controller implementation is efficient on it.

NVIDIA explains the relevant general mechanisms in its
[local-memory](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#local-memory)
and [branch-divergence](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#branching-and-divergence)
guidance. These mechanisms remain hypotheses until isolated measurements.

## Horizon, agents, minibatch: current implementation

`rollout_transitions = total_agents * horizon`.
`recurrent_sequences_per_minibatch = minibatch_size / horizon`.
`optimizer_steps = floor(replay_ratio * primary_agents * horizon / minibatch_size)`.
Only learner-bank rows count toward `primary_agents`. Epoch sampling requires
the learner rollout to divide evenly into minibatches; startup may snap the
requested size to satisfy this.

The old `2048*720` and current `4096*360` both collect 1,474,560 transitions.
With the same 0.75 frozen fraction, both have 921,600 learner transitions.
Old minibatch 2,880 and new 1,440 both contain four recurrent sequences, but
the latter makes about twice as many optimizer steps per rollout (621 vs 1,242
at replay ratio 1.94187546). Keeping minibatch 2,880 at horizon 360 would instead
use eight sequences and about the old optimizer-step count; that is an ablation,
not a requirement or a proven quality-preserving setting. It cannot repair
the dominant rollout cost.

Shorter horizon shortens the trained recurrent sequence/advantage window, not
the 720-turn episode. With `reset_every_horizon=0`, recurrent state carries
across rollout boundaries. Smaller H/L changes capacity and cannot load the
H512/L3 BC checkpoints. Neither change is guaranteed neutral to learning.

## Narrow measurement and acceptance plan

- First isolate CUDA time for neural forward, sampling, game step, and training
  on a quiescent GPU. Do not time a second job alongside the user's run.
- Compare the same native states, fixed logits, RNG seeds, complete actions and
  bank layout. Include fresh, crop-heavy, livestock-heavy, and mixed/reset
  states; initial single-worker farms alone miss planner scaling.
- Test decoder-local plan reuse and immutable capacity caching individually.
  Then examine scratch representation and bank-wide sampling. Changing fewer
  policies/strategies to get speed is not an equivalent optimization.
- Compare CPU/GPU actions, prefix masks, likelihoods, rewards, final state and
  RNG progression. Preserve job-order tie breaking, explicit-vs-chore priority,
  shed inventory order, exact market quantities, and simultaneous-trade rules.
- Re-run multi-executor, action-feasibility, real CUDA sampler/PPO-gradient and
  CPU/GPU adapter tests. Benchmark full H512/L3 geometry after warmup, then
  optionally compare end-to-end CPU simulation with the same workload.
- Keep the old runtime archive and all replay tapes/checkpoints. Do not replace
  the active binary/config while a trainer is running.

### Reproducible fixed-state sampler diagnostic

`tests/bench_kag_sampling.cu` includes the real BF16 production sampler and
GPU-environment sources. Run `bash
ocean/kaggriculture/deploy_20260921/bench_sampling.sh RESET_BANK` from a GPU
checkout. This builds a separate binary, refuses to run if a compute process
is already active, and bounds each invocation to 180 seconds. It never edits
the live configuration or replaces `puffer`.

It compares 2,048 and 4,096 rows, fresh states and evenly spaced real replay
reset states, and controllers 2/1 and 2/2 within the same new executable.
Each case uses fixed sinusoidal logits, identical Philox seeds, three warmups,
and twelve timed repeats. CUDA events time graph-captured sampling only;
RNG initialization and restoration of pristine base masks are outside the
timed region. Layouts are one combined sampler launch versus learner plus
eight frozen-bank launches with the production 0.75 frozen fraction.
Actions, masks, log probabilities, values, and final RNG state must be
byte-identical across layouts.

This isolates sampler launch geometry under fixed inputs, **not** a measured
end-to-end PPO speedup, trained-policy distribution, or optimization already
implemented in the trainer. It excludes neural forwards, decoder/environment
execution, and PPO. Controller 2/1 here still uses the new executable's kernel
resource footprint; it is not a rerun of the archived old executable.

### Measured diagnostic results (RTX 5060 Ti, September 21)

Completed in the user's `ssh_tmux` / `kag-bc` window after their runs stopped.
Raw remote log: `qualification/sampler_20260921.6Si3oi/results.log`.
The bank has 78,864 records; sampled states cover steps 0–718 and 1–16 units
per player, averaging 9.89 units at 2,048 rows and 9.88 at 4,096 rows.
Fresh states all have one unit. Timings below are median CUDA milliseconds
per complete set of rows, **excluding neural forward and environment step**.

| Rows | Fixture | 2/1, nine launches | 2/2, nine launches | 2/2, one launch | Sampling speed ratio |
| --- | --- | ---: | ---: | ---: | ---: |
| 2,048 | Fresh | 1.607 | 24.185 | 4.220 | 5.73x |
| 2,048 | Replay resets | 1.835 | 265.310 | 35.229 | 7.53x |
| 4,096 | Fresh | 2.231 | 29.254 | 4.261 | 6.86x |
| 4,096 | Replay resets | 2.529 | 286.643 | 35.973 | 7.97x |

All eight controller/fixture/row-count comparisons passed exact batched versus
banked parity for actions, prefix masks, log probabilities, values and final
Philox state. At 2,048 replay rows the new banked sampler repeats ranged from
264.999–265.594 ms, versus 35.142–35.318 ms combined. These are fixed-state,
fixed-logit measurements, not actual live-policy trajectories. They therefore
do not predict a 7.5x full-training improvement or explain every millisecond
of the earlier training logs.
`cuobjdump` confirms the diagnostic's sampler has the same 127 registers and
24,560-byte stack as the installed production sampler.

This promotes **bank-wide sampling after separate neural forwards** to a
well-supported first optimization candidate. Production still uses separate
decoder output allocations per bank; collecting those logits has a cost that
this diagnostic excludes. Preserve row/RNG mapping, heterogeneous frozen
network support, graph capture and archived masks when implementing it.
Even the combined new sampler remains much slower than 2/1 on these fixtures,
so capacity scans and planner complexity remain important second targets.
Decoder-local plan reuse independently targets the environment timer.

No runtime optimization was installed during this diagnostic. The production
binary and config remain unchanged, and the bounded benchmark left no GPU
training process running. Existing native-header narrowing and sweep-path
truncation compiler warnings were visible; compilation and all benchmark
parity checks completed successfully.

## Git handoff status at inspection

`pufferlib-multi-intent` is a detached worktree at `39f7993c5`, with the new
multi-intent/BC changes uncommitted. `pufferlib` is on
`feat/pokemon-personality-qd` at the same commit, with separate unrelated dirty
Retro/ARPG/Survivors/WebNav work. `5c` and live `origin/5c` are at `6abfb8dd4`;
the histories have 1 vs 12 unique commits. No branch switch, merge, commit or
push was performed during this diagnosis. Choose a scoped Kaggriculture/shared
trainer transfer or an explicit full merge; never force-checkout or reset the
dirty trees just to change the displayed branch name.

## Approved whole-branch merge completed

The user selected the whole development-branch merge and authorized pushing
`5c` after stopping the remote runs. Merge commit `5b06b9b50` has parents
`6abfb8dd4` (old `5c`) and `c551347ee` (development plus the multi-intent/BC
work). Conflicting older Retro/runtime files use the newer development port;
non-conflicting `5c` ARPG additions remain. The old generated `retro` binary
remains on disk but is no longer tracked, matching the development branch.

The main `/home/felix/puffertank/pufferlib` checkout is now on `5c`. Its 39
unrelated modified files and 177 originally untracked files were restored and
verified against their original Git blob hashes. They remain uncommitted (or
unchanged where already included by `5c`); they are not swept into the push.
Older local Kaggriculture drafts are superseded by the new implementation, with
the complete pre-switch working tree retained in stash
`2c6ca31eead833a219d3585d19e5ac4b0e648226` and the file/patch archive directory
`/home/felix/puffertank/backups/5c-merge-20260921.YzvPXa`.

Merged-code checks: `make -C ocean/kaggriculture CC=clang
BUILD=build/merge_5c_20260921 -j2 native-test multi-executor-test` passed,
including strict controller/observation contracts, action feasibility,
PLACE/shed parity, rewards, episode metrics, and multi-executor rollout.
Build/deployment shell syntax checks passed. This merge does not change the
remote installed binary, rewards, reset settings, or training configuration.
