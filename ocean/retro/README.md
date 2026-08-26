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
OBS_SIZE = 16 (ego) + 12*12 (pixels) = 160
```

* Ego (16 floats): `x,y`, player state, coins, score, time, world, stage,
  camera offset, flag, and death state.
* Window (144 values): a `12x12` framebuffer patch centered around Mario. The
  active real-ROM path samples QuickNES's indexed framebuffer and converts each
  value to a normalized float.

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
