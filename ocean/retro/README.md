# Retro: original-ROM SMB1 training

The default environment executes the original ROM in independent QuickNES
CPU/PPU/APU instances. It does not approximate Mario's physics or patch game
loops to recover from glitches. The policy runs on CUDA; emulation runs on CPU.

Supported SMB1 NTSC mapper-0 image, iNES file SHA-256:
`ec299b990e8bfee8ba46e3f61d63b2e1ae5b8a2e431de84e2e4bbd692dc53586`.
Use your own ROM at `ocean/retro/roms/smb1.nes`; other images fail explicitly.

## Build and run

Run from the Git repository, `/home/felix/puffertank/pufferlib`, not the older
duplicate directory one level above it.

```bash
make -C ocean/retro test -j2
./build.sh retro --fast                 # optimized standalone ROM runner
./retro levels                         # inspect all 32 starts
./retro play 4-2                        # arrows, X/Space=A, Z/Shift=B; R=reset
./build.sh retro                        # CUDA policy + CPU ROM environment
./puffer train retro
./retro watch latest --random           # new-contract checkpoint, all starts
```

For the native all-level hyperparameter sweep, run `./puffer sweep retro`.
See [SWEEP.md](SWEEP.md) for budgets, the sparse-aware ranking and how to
compare or extend promising trials. Rebuild with `./build.sh retro` first if
another environment has replaced the shared `./puffer` executable.

Without a display, play/watch run a bounded 600-decision smoke test.
Defaults: two recurrent layers of width 128, 1,024 environments, four workers.
Override starts with `env.spawn_levels=1-1,4-2,8-4`; single levels work too.

The new interface has **64 actions**, not 12. Old checkpoints are incompatible
and are not automatically loaded. New checkpoints go under
`checkpoints/retro_rom/retro/`. The watcher checks tensor sizes against
`config/retro.ini`; use the same policy width/layers as training.

## Fidelity contract and training choices

- Gameplay executes ROM instructions. Integer/subpixel arithmetic, backwards
  acceleration, controller polling, collision and warp logic are not
  reimplemented. No gameplay RAM writes occur during stepping.
- `frameskip=1` permits input every emulated frame. Action bits represent A, B,
  Up, Down, Left, Right, including opposite directions:
  `mask = (action & 3) | ((action & 60) << 2)`.
  Start/Select are excluded from training but available to raw replay.
- Reset setup writes world, stage and area in a title snapshot, then lets the
  ROM perform its entrance. All 32 first-controllable starts are prebuilt.
  Actual area and level-data pointers are validated, not just labels.
  Some original stages reuse geometry.
- Reset restores complete emulator state plus its matching image and palette.
  This is a training start distribution, not a claim that a level start equals
  every possible state reached there during uninterrupted play.
- Flags and ordinary level changes do not reset the game. Wrong warps are not
  repaired. A stalled game frame counter is not treated as a death.
- Training ends on ROM death/game-over, final castle completion, or
  `max_frames=30000`. The horizon is logged separately from deaths. The
  trainer ABI has one done bit: this is a finite-horizon task terminal, not a
  bootstrapped time-limit mask. Raw `retro_frame()` has none of these limits.

**ROM execution is not proof that QuickNES is hardware-perfect.** Tests
compare optimized and unoptimized execution of this same core. Independent
emulator/hardware comparison and recorded glitch tapes remain acceptance
gates before claiming every exploit is reproduced. Preserving an exploit and
getting PPO to discover it are different problems.

The independent cold-boot diagnostic, `make -C ocean/retro reference-probe`,
currently reports RAM/RNG differences from the local FCEUmm core. Sampled Mario
positions/subpixels matched after gameplay began, but this is NOT full parity.
The diagnostic intentionally does not hide differing bytes or call its exit
status a parity pass. Power-on state and controller/frame phase must be aligned
before determining which remaining differences are emulation inaccuracies.

The vendored CPU also approximates some unsupported opcodes as NOPs. The ROM
wrapper now **fails explicitly** if that path is reached, including at boot,
instead of silently training on altered behavior. Ordinary game-code loops
remain emulated. Hardware CPU-jam/unsupported-opcode exploits need proper core
support before they can be trained; this is a known limit, not a repaired hang.

## Speed work

- The core is compiled once into a cached host-compiler archive. The native-C
  SMB port, SMB2J objects, patches, signals and hang watchdogs are absent from
  the default build. Unused reference cores and prototypes are not loaded.
- Cartridge bytes and decoded immutable CHR tiles are shared. CPU, RAM, PPU,
  APU, controller and mapper state remain private. CHR cache ownership is
  reference-counted, with copy-on-write for explicit cache modifications.
- Repeated ROM jump-to-itself dispatches are folded up to the next scheduled
  emulator event. Folding advances by whole three-cycle JMP instructions,
  preserving the original cycle overshoot. Disable with
  `env.idle_loop_skip=0`. This does not skip game frames or reset frozen games.
- Workers rebind private scratch framebuffers every frame. At frameskip > 1,
  intermediate frames can omit final pixels while still executing emulation.
- Human display uploads a texture instead of drawing rectangles per pixel.
  Training requires no display/audio playback; sound hardware is emulated.

```bash
# envs, decisions/env, workers, frameskip, starts, inputs, idle folding
./retro bench 512 512 4 1 all random 1
./retro bench 512 512 4 1 all random 0
./retro bench 512 512 4 4 all random 1
```

Benchmarks exclude initialization but include observations, terminals and
resets through the actual native vector constructor. They report decisions/s
and actual ROM frames/s separately. Environment-only SPS is not PPO SPS.
Four-frame holds restrict frame-perfect inputs: they are a different control
task, even though all four frames are emulated.

The bounded 2026-09-07 PPO run completed 1,048,576 one-frame training decisions
in 60.703 seconds: **17.3k actual PPO SPS**, with about 0.945 GiB trainer-reported
VRAM. Checkpoint reload passed. This short run recorded no level clears; it
establishes throughput/stability, not learned competence. See
`REALIGNMENT_20260907.md` for the configuration, artifacts and comparison caveats.

## Observation and reward

OBS 256 = 64 RAM/physics features + 48 entity features + a 12×12 luma window
(each cell averages an 8×8 pixel block). Features include subpixels, signed
velocity, scrolling and warp state. This is a compact partial observation;
the policy is recurrent.

The reward combines retained distance checkpoints with soft potential shaping.
Every `env.checkpoint_distance=128` novel pixels earns
`env.checkpoint_reward=0.125` raw reward. Novel pixels accumulate across area
frontiers; revisiting the same level/loaded-area data retains its high-water
mark. First arrival establishes a baseline, so entering an area does not pay
for its starting coordinate. Each frontier is capped at x=3400 to bound
coordinate-wrap jackpots; 256 frontier records per episode are available,
after which new identities earn no distance reward. This bookkeeping never
writes ROM state. It measures new forward exploration, not calibrated route
length or credit for every level skipped by a warp.

The unscaled weights are +10 for completion and −0.125 for death, with score
reward disabled. `env.reward_scale=0.0625` multiplies the **entire** reward:
checkpoints contribute +0.0078125 each, completion +0.625, death −0.0078125.
Earned checkpoint rewards survive death/timeouts; two checkpoints outweigh
one death penalty before shaping/discounting. Set `checkpoint_reward=0` to
ablate the hard reward. These defaults intentionally change the old sparse
objective to encourage exploration; the old sweep is not a validation of them.
Completion pays once per source level per episode on a forward level/world
transition or castle completion.
A warp counts as advancing from its source, not playing every skipped level.
Per-level episode starts and completion events are separate: their ratio is
not automatically a success rate, since an episode can traverse several levels.

Shaping is `gamma * Phi(next_x) - Phi(previous_x)`, where
`Phi(x)=clamp(x/3400,0,1)` and terminal potential is zero. The difference is
retained across area changes. The native launcher automatically sets
`env.potential_gamma = train.gamma` after CLI overrides, including sweep trials.
Other entry points must keep these equal themselves.

`train.reward_clip=1` stays enabled. With the default weights and one-frame
controls, the combined returned reward lies within [−0.0703125, +0.8984375], so the
clip does not distort reward ratios. The native launcher checks a conservative
bound and rejects unsafe scales/weights. Raising frameskip or enabling score
reward may require reducing the common scale. Episode return sums actual
scaled rewards; old unscaled returns are not directly comparable. The earlier
throughput smoke test predates this reward rescaling. `progress_pixels` and
`checkpoints` report episode totals; `frames` and `decisions` also report full
episodes rather than the final logging window. Start a fresh policy for this
reward experiment (`base.load_model_path=None`); already-running processes
continue using their loaded code and settings until restarted.

## Validation and experimental code

`make -C ocean/retro test` covers all starts, exact reset state/images,
single-level selection, controller combinations, flag/powerup RAM slots,
horizons and return accounting. It compares RAM every frame and canonical
images/serialized state every four frames across moving, jumping, backwards,
idle and random trajectories. It toggles idle folding, CHR sharing and
intermediate rendering, and repeatedly restores only one side. It also
compares one-worker/four-worker vectors through resets.
It also checks clipping-safe reward scaling, sweep configuration and the
clear-first panel score/action decoder.

AddressSanitizer build (separate archive, leaving optimized binaries intact):

```bash
make -C ocean/retro test -j2 BUILD=../../build/retro-asan \
  'CXXFLAGS=-O1 -g -march=native -std=c++17 -fsanitize=address -fno-omit-frame-pointer'
```

```bash
./retro replay INPUT.txt 4-2
```

Replay takes one decimal NES mask (0–255) per frame and prints frame, input,
RAM hash, world, stage and X. It never shapes rewards, ends or resets the game.
This is the entry point for future recorded glitch tapes.

Old sources remain in `retro_legacy.h`, `retro_legacy.c`, `retro_fast.h` and
`smb_patches/` for recovery/experiments. Opt in with
`RETRO_LEGACY=1 ./build.sh retro --fast`; the old 12-action configuration is
separate and has no exactness guarantee. Policy transfer is not frame parity.

A CUDA successor should be tested ROM execution or equivalent instruction
translation, not float-physics recreation. Integer subpixels alone are not
enough: carry flags, RAM aliasing, input timing, interrupts and PPU interactions
matter. Gate optimizations on differential replay, then measured throughput.
