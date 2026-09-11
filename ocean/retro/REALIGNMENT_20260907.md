# ROM training realignment — 2026-09-07

Implemented in `/home/felix/puffertank/pufferlib`. The earlier
`REVIEW_20260907.md` is a historical review of the pre-change implementation.
Unrelated worktree changes and existing checkpoints were preserved. After CPU
measurements, the user approved stopping the old fast-backend trainer for a
bounded ROM PPO benchmark. Its latest saved checkpoint remains at
`checkpoints/retro/1788800418624/0000000281804800.bin` (281,804,800 steps).
Only unsaved work after that checkpoint was discarded when stopping the run.

## Measured speed

Four CPU workers on this machine; 512 environments. These are environment-only
benchmarks, not PPO training SPS. Initialization is excluded. New benchmarks
count actual emulated frames and include observations and resets.

| Path / input workload | Frames per decision | Decisions/s | ROM frames/s |
| --- | ---: | ---: | ---: |
| Old native-C port, 1-1, right only | 4 | 37,050 | about 148,198 |
| Old ROM wrapper, 1-1, right only | 4 | 6,401 | reported 25,605 |
| New ROM wrapper, all 32 starts, random, idle fold off | 1 | 14,527 | 14,527 |
| New ROM wrapper, all 32 starts, random, idle fold on | 1 | 28,956 | 28,956 |
| New ROM wrapper, 1-1, right only, idle fold on | 4 | 7,170 | 28,513 |
| New ROM wrapper, all starts, random, idle fold on | 4 | 7,197 | 28,762 |

The original two rows were measured before changing the configuration, using
the original binary; its frame count is nominal, not corrected for hidden
wrapper frames or early terminals. The new fold on/off pair used 256 decisions
per environment and produced the same checksum, 278449. The four-frame new
rows used 128 decisions/environment. These are single trials, not confidence
intervals; game trajectory, CPU contention and clocking affect throughput.

The direct idle-fold comparison is a 1.99× environment speedup. On the simple
four-frame right-only workload, the old native port remains about 5.2× faster
than the new ROM wrapper. One-frame controls make decision-SPS look much higher
than four-frame controls without simulating more game time; do not conflate
those measures. The historical ~2k ROM PPO SPS cannot be directly compared to
these environment-only rates.

An earlier corrected-wrapper measurement was only ~9.3k one-frame SPS while
other build work was finishing. The later controlled fold-off result was
14.5k. Use the matched on/off pair to attribute optimization gains.

## Changes

- Default build is ROM-only. The patched native-C game port and SMB2J/watchdog
  machinery require an explicit legacy build; old source and checkpoints remain.
- All 32 level starts are prebuilt and validated against actual area/data
  pointers. Single-level selection, zero RNG seeds, reset image ownership and
  thread-safe state-bank publication are fixed.
- Default controls are one-frame, all 64 gameplay button combinations. Raw
  replay also accepts Start/Select and never ends or resets the game.
- Gameplay RAM pokes and frame-counter hang resets are removed. Flags, pipes,
  ordinary level transitions and wrong warps can continue in the ROM.
- Completion-centered rewards replace score/kill/idle incentives. Returns sum
  every reward. Potential shaping includes area transitions and zero terminal
  potential. Horizon truncations are distinguished from deaths in metrics.
- Shared immutable cartridge/CHR caches, cached host compilation and exact
  self-JMP dispatch folding reduce overhead without a physics rewrite.
- Human rendering uses a texture. Only watched environments retain their own
  display image, preventing other worker environments from overwriting it.
- Defaults use 1,024 environments, two recurrent layers, width 128 and horizon
  64. New checkpoints are isolated under `checkpoints/retro_rom/retro/`.

## Validation and unresolved fidelity gates

Optimized/unoptimized same-core tests pass 65,536 frames across all 32 starts,
comparing RAM every frame and canonical images/serialized state every fourth
frame. Coverage includes forward jumping, backwards motion, idle/random input,
pause, repeated save/load, full versus skipped intermediate rendering and
private versus shared CHR. Separate tests cover exact resets, controller masks,
flag/powerup RAM addresses, horizon/return accounting and one/four-worker
reproducibility. An AddressSanitizer run passed after test-fixture dictionary
cleanup. The standalone 8-4 headless smoke test passed; incompatible old
checkpoints are rejected by the watcher.

This is not a 100% hardware-exact certificate. The independent FCEUmm cold-boot
probe found RAM/RNG and boot-phase differences. Sampled Mario positions and
subpixels matched during the movement sequence, but full RAM did not. Align
power-on state and frame/controller phase before diagnosing remaining core
differences. Preserve these differences as evidence, not ignored bytes.

The vendored CPU approximates some unsupported opcodes. Boot and stepping now
fail explicitly if that path is reached; no such errors occurred in the parity
test. Hardware-jam/unsupported-opcode exploits require proper implementation,
not a watchdog or silently continuing with NOPs.

Known wrong-warp/clip input tapes and independent-reference comparisons remain
required before claiming those specific exploits are certified. No learning
result or natural glitch discovery has been demonstrated by these tests.

## End-to-end PPO benchmark

The user-authorized benchmark completed successfully on the GTX 1060 3 GB:

- 1,048,576 training decisions / ROM frames in **60.703 seconds**, including
  the training loop's warm-up and pipeline drain: **17,273 actual PPO SPS**.
- 1,024 environments, four workers, all 32 starts, one-frame controls, 64
  actions, two recurrent layers of width 128, horizon 64, replay ratio 3.
- All 16 learner updates had finite losses/gradients and zero reported
  nonfinite importance ratios. No unsupported-opcode error occurred.
- Trainer-reported VRAM was about **0.945 GiB**. This is the trainer's metric,
  not a guarantee about all host/Windows GPU allocations under WSL.
- Final checkpoint is
  `checkpoints/retro_rom/retro/rom_smoke_20260907/0000000001048576.bin`.
  The standalone deterministic watcher successfully reloaded it and ran
  600 decisions with random level selection.
- Persisted configuration/metrics: `logs/retro/rom_smoke_20260907.ini`.
- No level clears or warps were recorded. This is a throughput/stability
  result, not evidence of a competent policy or natural glitch discovery.

The final live dashboard displayed 349k SPS during the pipeline-only drain;
that is NOT sustainable training throughput. The 17.3k figure uses total
training steps divided by the persisted 60.703-second training uptime. The
post-training evaluation is separate and is not included in that denominator.
Individual dashboard intervals varied substantially, so report the aggregate.

17.3k is about 8.6 times the historical ~2k decision-SPS figure, but the control
contract changed: the new run makes one decision per ROM frame, whereas the
old setup held inputs for four frames. This is not an 8.6× claim about simulated
game-time throughput or learning sample efficiency.

The bounded run has exited. A long 500M-step training run was not started.
At this short-run rate, 500M training frames would take about eight hours,
excluding additional evaluation; real throughput can change with trajectories.

Benchmark command used:

```bash
./puffer train retro train.total_timesteps=1048576 \
  base.run_id=rom_smoke_20260907 base.checkpoint_interval=16 \
  base.eval_episodes=32 base.ppo_debug=1
```

To continue this new-contract policy (not an old 12-action checkpoint):

```bash
./puffer train retro base.load_model_path=checkpoints/retro_rom/retro/rom_smoke_20260907/0000000001048576.bin
```

This restores policy weights, not an exact optimizer/RNG/rollout resume. Preserve
a one-frame evaluation/replay contract if trying four-frame pretraining.

For a CUDA successor, preserve ROM instruction semantics, flags, memory
aliasing and interrupt/controller/PPU timing. Integer subpixels alone are not
enough. Require differential replay and measured speedup before replacing the
current execution engine. First make ordinary level completion reliable; then
add replay seeds/exploration aimed at rare states without changing physics.
