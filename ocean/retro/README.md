# Retro (Super Mario Bros) - PufferLib Ocean Env

C/C++ only. The active training path runs the real `smb1.nes` ROM through one
independent QuickNES instance per environment. There is no Python emulator path
and no procedural level substitute.

## Layout

* `retro.h` - real-ROM CPU environment and the unchanged PufferLib env ABI. The
  immutable cartridge is loaded once, QuickNES objects are contiguous in the
  native vector arena, and the first `frameskip - 1` frames use QuickNES
  skip-render mode.
* `retro.c` - standalone CPU eval/demo entry (`./build.sh retro --local/fast`).
* `config/retro.ini` - train defaults (`puffer train retro`).

The old procedural CUDA prototype is not used. A CUDA port of the complete
6502 CPU, PPU, APU, mapper, and SMB timing would be a separate emulator and is
not enabled because replacing it with simplified physics would lose enemies,
secrets, scrolling behavior, and other ROM logic.

## Observation

```
OBS_SIZE = 64 (ego/physics) + 48 (entities) + 12*12 (pixels) = 256
```

* Ego (64): absolute/page/offset/subpixel X (`$6D/$86/$0400/$0705`), Y
  (`$B5/$CE/$0433`), signed X/Y velocities (`$57/$9F`), ground/engine state
  (`$1D/$0E`), facing, size/power, duck/swim/jump/invuln/star timers, scroll
  (`$073F/$071A-$071D/$0775`/lock), area+warp pointers (`$0750/$06D6/$072C/
  $0739`), world/level/area, timer, frame counter, RNG (`$07A7`), side
  collision, coins/score/tick/flag/dead/lives. This is the screen-X vs scroll
  desync state behind 4-2 wrong warp, minus-world wall clips, bump warps,
  and the subpixel/velocity windows behind wall clips, flagpole and vine
  glitches. All RAM, so the same builder runs on libretro
  (`RETRO_MEMORY_SYSTEM_RAM`).
* Entities (48): 5 enemy slots x 8 (active, type `$16`, state `$1E`,
  dx/dy vs Mario, dir, x/y speed) + powerup/fireball dx/dy/type/state.
* Window (144 values): a `12x12` framebuffer patch centered around Mario. The
  active real-ROM path samples QuickNES's indexed framebuffer and converts each
  value to a normalized float.

## Reward

```
progress = γ·Φ(x') − Φ(x),  Φ(x) = clamp(x/3400), γ = potential_gamma = train.gamma
```

Potential-based shaping (Ng–Harada–Russell): telescopes over the episode so
the optimal policy is unchanged; retreat is penalized symmetrically. Gated to
the same (world,stage,area) so warps reset the basis. Sparse events on top:
score/100, coin +0.5, death −2.5, flag +5, new-area +2. `x_pos_max` is kept
for the distance log only.

## Actions

Single discrete head `ACT_SIZES {12}`:

```
0 NOOP
1 RIGHT           5 A                9 B
2 RIGHT+A         6 LEFT            10 UP
3 RIGHT+B         7 LEFT+A           11 RIGHT+DOWN
4 RIGHT+A+B       8 DOWN
```

These map to the NES joypad mask (`A/B/Up/Down/Left/Right`). PPO still samples
one normal discrete action per environment. Start and Select are not in the
training action set.

## CPU Throughput Notes

* **Shared cartridge.** The ROM is parsed once. Each environment gets
  independent CPU, PPU, APU, RAM, nametable, sprite, mapper, and save-state
  data; only immutable cartridge bytes are shared.
* **Skip intermediate rendering.** With `frameskip = 4`, QuickNES still
  executes all four complete frames, but only the fourth writes the framebuffer
  used by the observation. Set `RETRO_FULL_RENDER=1` to render every frame for
  diagnostics.
* **Contiguous emulator arena.** Native training constructs the QuickNES
  objects in one contiguous array. PPO buffers and the `Env` ABI remain
  unchanged, while emulator state is less scattered in memory.
* **Thread-local framebuffer binding.** Workers rebind each emulator to their
  own scratch framebuffer before stepping, avoiding cross-thread pixel races.

## Backends (`env.backend`, default `quicknes`)

| backend | core | speed (4096 envs, 4 workers) | use for |
|---|---|---|---|
| `quicknes` | QuickNES interpreter (this repo) | ~11.5k env_steps/s | eval, `watch`, readable preview, ground truth |
| `fast` | native-C smbcore, fetched at build time (pinned `87af9a3`) | ~44k env_steps/s bench, ~21.5k SPS training | training |

Select per run: `./retro bench 4096 16 4 fast`, `./puffer train retro env.backend=fast`.
Same ROM (`ocean/retro/roms/smb1.nes`), same RAM map, same `OBS 256` / 12-action
ABI, shared `retro_obs.h` builder. The fast core keeps upstream's
bug-compatible game logic (minus world / clipping bugs in scope) but runs it
as compiled C with no APU synthesis and rasterizes only the 12x12 obs patch
on observed frames.

Known cross-backend gap (measured, honest): a ~1-frame input/physics phase
offset from different NMI/joypad-latch ordering, so full-RAM hashes never
match bit-exact. Train on `fast`, confirm transfer by evaluating the policy
under `quicknes`/libretro -- closed-loop PPO acting every 4 frames absorbs
the phase gap; frame-perfect input tapes do NOT transfer 1:1.

Robustness: both backends convert an input-dead freeze state (frame counter
`$0009` stalled across a tick -- SMB1 has several documented ones, and random
exploration finds them) into a terminal + reset, so one frozen env can never
wedge a rollout. Hunt regressions with the chaos monkey:
`./retro chaos 1024 2000 4 fast 12345` (deterministic seed, random actions).

### Why no CUDA env kernel

Single-core tick rate is ~800k frames/s (game logic) vs ~15k for the
interpreter, so 4 CPU cores already feed the GPU policy loop (training is
policy-bound past ~20k SPS). A branch-heavy logic port would be
divergence- and launch-overhead-bound on GPU. SoA note: vectorization is at
env level (contiguous compact states, ~18KB varying/env); field-wise SoA of
the game state is impossible without rewriting smbcore (it indexes RAM
arrays).

### Why There Is No CUDA ROM Kernel Yet

The full ROM emulator is stateful and branch-heavy. Moving it to CUDA while
preserving behavior would require porting the complete 6502/PPU/APU/mapper
state machine, not just changing `Env` from AoS to SoA. The vendored libretro
cores remain available for comparison, but the active path directly uses
QuickNES so each environment is independent and does not serialize through a
singleton core.

## Build and Run

```bash
# CPU standalone demo
./build.sh retro --fast
./retro

# CPU native train binary
./build.sh retro
./puffer train retro
```

The standalone commands are:

```bash
./retro                         # human play
./retro play                    # human play
./retro watch latest            # watch newest policy checkpoint
./retro watch PATH.bin         # watch a specific checkpoint
```

Set `DISPLAY=` and `WAYLAND_DISPLAY=` to use the 100-step headless smoke demo.
