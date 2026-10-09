# Retro conversion to PufferLib 5.0

The new [Mario learning experiment register](EXPERIMENTS.md) tracks the
procedural CUDA simulator, visual/TAS pretraining, demonstration resets,
Go-Explore on CUDA and ROM backends, QD and interpretability. The first new
implementation is [Mario Lab](../mario_lab/README.md).

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

The formerly ignored `nes_emu/gen_nes_ntsc_palette_fixed.py` is now tracked.
Its stdout reproduces `nes_emu/nes_ntsc_palette_fixed.h` byte-for-byte; this
generator needs only Python's standard library, not a ROM or emulator build.

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

## Exploration rewards

`env.novel_area_reward` pays once for each new loaded, playable destination
within a level per episode, independent of HUD time and elapsed frames. It
defaults to zero when omitted; the exploration config enables raw reward 1
(0.0625 after `reward_scale`). Starting areas, level-entry areas and revisits
do not earn this bonus. The `novel_areas` metric counts eligible events even
when the reward is disabled. Detection runs every native frame, including
inside frameskip-4 actions. Timed `pipe_segment_bonus` is separate and disabled.

`spawn_levels=all` samples among 32 level starts; it does not always start at
1-1. `max_frames` counts native frames, not actions, regardless of frameskip.
Training continues through deaths and level clears. The ROM handles life loss
and midpoint respawns; a new training episode starts at game over, the final
win, or the native-frame cap. The distance-based `checkpoint_reward` is only a
reward milestone and does not create a ROM respawn point.

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
The default config now starts fresh 256×2 policies for all-level exploration,
not the old pipe-exit practice curriculum. It does not use the legacy custom
optimizer or priority replay.

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

### Balanced native Protein sweep

`./puffer_retro sweep` (or the Retro-built `./puffer sweep`) now supports an
opt-in `[panel]` post-training evaluation. The config requests 500 successful
trials of 30M nominal decisions each, from fresh weights, with 27 varied resource,
PPO and reward dimensions. Threads/buffers remain 16/4; architecture, observation
resolution, ROM, frameskip and evaluation protocol do not vary. Invalid/OOM
samples still fail normally and feed Protein's failure model. Native training
rounds the decision budget down to complete rollout batches.

Each final checkpoint is evaluated on exactly eight attempts per level, all
32 levels, fixed per-attempt sampling seeds, four-frame actions and a 30,000
native-frame budget. Natural lives/midpoint respawns remain enabled; each attempt
ends at its first source-level clear, game over or timeout. `panel` does not
balance training resets; it replaces the score sent back to Protein with a
balanced **evaluation** score. It is not a full-game or warp-route evaluation.

The default objective, `checkpoints`, is mean episode-local forward-frontier
progress checkpoints (128 pixels each), including failed attempts and additional
areas, not raw game score or shaped return. Already visited progress cannot pay
again after respawn. Checkpoint spacing is fixed inside evaluation, independently
of training reward parameters. This supplies signal before fresh policies clear
levels. `perf` selects the existing clear-first panel score instead; `speed`
still means personal-best clear time, **not** average time. The latter objectives
should not be confused with the fresh-policy progress sweep.

Every final model receives a `.bin.panel.tsv` report with level, replicate,
clear outcome, elapsed frames and progress checkpoints. Final INI metrics include
`panel/score`, `panel/clear_rate`, `panel/clear_frames`, `panel/attempts` and
`panel/checkpoints`. Ordinary training-window `env/*` metrics remain separate.
Protein receives one panel score and total train-plus-panel cost, regardless of
`sweep.downsample`. The checkpoint's resolved config is passed to the evaluator;
the panel does not silently reread new training settings during a sweep.

Set `panel.repeats=0` to disable post-training evaluation. To fine-tune, set
`base.load_model_path` to one explicit preserved checkpoint, never `latest`.
Every candidate then starts from that same parent; no automatic hill-climb
promotion occurs. The default config freezes the inherited model-size and
step-budget sweep sections explicitly: if changing `train.total_timesteps`, also
change both bounds of `[sweep.train.total_timesteps]`.

`build.sh retro` also builds the CPU panel. The shared trainer adds only an
optional post-training evaluation hook, after training workers stop; no loss,
optimizer, sampler or rollout selection changes are part of this integration.
Evaluation failure fails the trial instead of falling back to training score.

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

Interactive visual qualification, BF16 qualification and average-completion-time
objective design still need
porting and qualification. The original documentation and experiment assets
remain in `archive/5c-before-unification-20260924`; their old CLI/build commands
are not instructions for this 5.0 checkout. The network port adds only the
standard custom-encoder registration to `src/ocean.cu`; it changes no shared
loss, optimizer or rollout logic.

Upstream's trainer has no equivalent of the legacy `PUF_SWEEP_SCORE` callback.
This fork now exposes the optional post-training hook described above, without
restoring the retired optimizer or using training-window averages as panel scores.

QuickNES-derived source retains its original copyright notices and LGPL-2.1+
terms; a license copy is in `nes_emu/COPYING`. Individual third-party files
retain their own notices. The old checked-in `tools/proto` executable is not
imported; its source tools are preserved for rebuilding.
