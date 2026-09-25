# Retro conversion to PufferLib 5.0

Conversion is in progress. The full-screen SMB1 simulator, QuickNES source,
compiled-ROM block generator, natural-life playback helpers and practice
controller replay are preserved from `5c` at `036cf4251`. Native FP32 training
is available with the matching CNN, along with the standalone play/watch viewer.
Do not substitute the generic MLP or use an incompatible checkpoint.

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
borrowed-input/CUDA-graph checks. BF16 remains unqualified.

```sh
make -C ocean/retro policy-test
make -C ocean/retro policy-legacy-test LEGACY_POLICY=/path/to/legacy/ocean/retro/retro_policy_cpu.h
make -C ocean/retro encoder-test
./build/retro/test_encoder
```

The legacy comparison requires the preserved checkout and its original CPU
inference header. `encoder-test` builds the CUDA test; the following command
runs it. Use a separate `BUILD` directory when changing `RETRO_OBS_SCALE`.

## Native training

```sh
bash build.sh retro --float
./puffer train
```

The build generates compiled-ROM blocks from the local cartridge and links
QuickNES. Simulation runs on CPU; the CNN and upstream trainer run on GPU.
The config preserves the legacy 256×2 pipe-exit practice setup and rewards,
but not its custom optimizer or retired priority-replay settings. The old
speed-ranked sweep panel is not integrated yet.

Isolated FP32 smoke tests completed 1,024 steps with a small network and graphs
off, and 2,048 steps with the 256×2 network, graphs on and 64-frame timeouts.
Both used asynchronous training with two buffers, finite losses and saved final
checkpoints. These establish execution, not learning quality or throughput.

## Play/watch and policy inspection

```sh
make -C ocean/retro viewer
./build/retro/viewer play --inspect
./build/retro/viewer watch /path/to/checkpoint.bin --deterministic --inspect-check
```

The viewer preserves natural-life playback, manual controls, replay tapes,
timing output and the full-screen input inspector. `--inspect-check` runs
600 headless decisions with short episodes and verifies the policy's image
against source pixels through resets. A legacy 256×2 learned checkpoint
passes this check. Interactive rendering still needs visual qualification.
The checkpoint must match the architecture in `config/retro.ini`; `latest`
searches that config's checkpoint directory. See `viewer --help` for controls.

CPU inference is compiled as C and connected to the C++ emulator through
`retro_policy_api.c`; no upstream CPU inference changes are required.

## Repeatable checkpoint panel

```sh
make -C ocean/retro panel panel-test
./build/retro/sweep_eval /path/to/checkpoint.bin --config /path/to/run.ini \
  --metric speed --frames 1800 --repeats 4 --workers 4 --output /tmp/retro_panel.tsv
```

Use the checkpoint's actual configuration, not a different current experiment.
The panel preserves per-attempt random seeds across worker counts, shares
read-only weights, and gives each attempt independent recurrent state.
`speed` ranks best clear time first, then mean successful time; it does not
reward clear rate. `perf` ranks clear count before forward progress, and
`distance` measures forward pixels. Practice times are labelled segment times,
not full-run RTA. `--full-run` disables practice and `--levels all` selects all
32 starts. `--deterministic` uses argmax actions.

A learned 256×2 checkpoint produced byte-identical four-attempt reports with
one and four workers: three clears, best 1,605 frames, mean 1,661 frames.
The action-layout/sampling and metric-ordering unit tests also pass. This
standalone panel is not yet connected to native Protein sweep scoring.

## Recreating the practice tape

```sh
make -C ocean/retro capture
./build/retro/capture_practice /path/to/parent.bin /path/to/new.inputs
```

This runs the parent from the ordinary 1-1 start with the original seeded
sampled-action stream, recording real controller inputs until the first
return-pipe black frame. It never edits ROM RAM and refuses an existing output
path. The parent must match `config/retro.ini`'s network architecture and must
actually reach the pipe; an arbitrary practice-trained model may not.

Qualification used the preserved `sweep_1790050031940_0012` parent: the resulting
823-frame tape matches `practice/pipe_exit.inputs` byte-for-byte. Its capture
state is TIME 376, routine 2, bus phase 19 and X 208. Replay files contain no
ROM bytes; their header binds them to the verified cartridge fingerprint.

## Remaining conversion

Interactive visual qualification, BF16 qualification and automatic speed
sweep integration still need
porting and qualification. The original documentation and experiment assets
remain in `archive/5c-before-unification-20260924`; their old CLI/build commands
are not instructions for this 5.0 checkout. The network port adds only the
standard custom-encoder registration to `src/ocean.cu`; it changes no shared
loss, optimizer or rollout logic.

Upstream's trainer has no equivalent of the legacy `PUF_SWEEP_SCORE` callback.
Connecting the standalone speed panel to Protein therefore needs a separately
reviewed integration decision; ordinary training scores are not substitutes
for this fixed-panel speed objective.

QuickNES-derived source retains its original copyright notices and LGPL-2.1+
terms; a license copy is in `nes_emu/COPYING`. Individual third-party files
retain their own notices. The old checked-in `tools/proto` executable is not
imported; its source tools are preserved for rebuilding.
