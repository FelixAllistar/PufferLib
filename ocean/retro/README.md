# Retro conversion to PufferLib 5.0

Conversion is in progress. The full-screen SMB1 simulator, QuickNES source,
compiled-ROM block generator, natural-life playback helpers and practice
controller replay are preserved from `5c` at `036cf4251`. Native training and
policy evaluation are explicitly blocked until native integration is qualified;
do not substitute the generic MLP or use an incompatible checkpoint.

## Local assets and simulator tests

Supply your own verified World/NTSC cartridge at
`ocean/retro/roms/smb1_ntsc.nes`. This directory is Git-ignored. The accepted
SHA-1 identities are `33d23c2f2cfa4c9efec87f7bc1321ce3ce6c89bd` (NES 2.0)
and `ea343f4e445a9050d4b4fbac2c77d0693b1d0922` (iNES). PAL and modified images
are rejected. No ROM download or ROM bytes are included in this port.

With Clang++, OpenMP, Python 3, GNU Make and the repository's Raylib 5.5
dependency available, run from the repository root:

```sh
make -C ocean/retro -j2 test
make -C ocean/retro -j2 batch-test
```

The first builds the reference emulator. The second generates CPU instruction
blocks from your cartridge under ignored `build/retro_batch/`, then compares
them with the interpreter. Generated files contain ROM-derived material;
keep them out of Git and distribute neither them nor the cartridge.

Observations remain 112 RAM features plus the entire 64×60 luminance image
(3,952 values), with 64 controller combinations. `RETRO_OBS_SCALE=2` selects
128×120 at build time and requires a matching network/checkpoint. No crop or
runtime resolution switch is introduced.

The practice fixture is controller input, not a savestate or ROM patch. It
replays to the return-pipe black screen and caches the resulting state. Its
clock measures a segment, not a full-run speedrun time. Tests cover natural
level starts, rewards, terminal/reset behavior, practice restoration and
natural-life playback; CPU block tests also compare instructions, complete
emulator states and all observation pixels.

Two small emulator fixes accompany the port: pixel-word accesses declare
byte alignment, and audio snapshots initialize their two unused bytes. The
latter prevents serialized reset states from depending on stack contents;
neither changes game rules, rewards or observation construction. Regression
tests check alignment and serialize the same audio state into differently
prefilled destinations.

For a fully instrumented reference build in a separate output directory:

```sh
UBSAN_OPTIONS=halt_on_error=1 make -C ocean/retro -j2 test \
  BUILD=../../build/retro_sanitize \
  CXXFLAGS='-O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer'
```

## Network qualification

The preserved three-convolution image branch, RAM branch and fusion layer now
use the 5.0 encoder interface and upstream recurrent network/head. Parameter
ordering is unchanged. CPU output traces match the legacy model exactly at
both supported image resolutions, including recurrent resets. The scale-4
FP32 CUDA suite passes feature-map, recurrent-output, numerical-gradient and
borrowed-input/CUDA-graph checks. BF16 and native training remain unqualified.

```sh
make -C ocean/retro policy-test
make -C ocean/retro policy-legacy-test LEGACY_POLICY=/path/to/legacy/ocean/retro/retro_policy_cpu.h
make -C ocean/retro encoder-test
./build/retro/test_encoder
```

The legacy comparison requires the preserved checkout and its original CPU
inference header. `encoder-test` builds the CUDA test; the following command
runs it. Use a separate `BUILD` directory when changing `RETRO_OBS_SCALE`.

## Remaining conversion

Native trainer/build integration, the play/watch inspector, learned-checkpoint qualification, speed
evaluation panels, sweep integration and training configuration still need
porting and qualification. The original documentation and experiment assets
remain in `archive/5c-before-unification-20260924`; their old CLI/build commands
are not instructions for this 5.0 checkout. The network port adds only the
standard custom-encoder registration to `src/ocean.cu`; it changes no shared
loss, optimizer or rollout logic.

QuickNES-derived source retains its original copyright notices and LGPL-2.1+
terms; a license copy is in `nes_emu/COPYING`. Individual third-party files
retain their own notices. The old checked-in `tools/proto` executable is not
imported; its source tools are preserved for rebuilding.
