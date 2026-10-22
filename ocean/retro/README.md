# Retro conversion to PufferLib 5.0

Current experiment and operator handoff: [Retro ROM training handoff](../../reports/retro/archive_20261019/HANDOFF.md), including the live sweep, policy ancestry, Vast installation, and recovery procedures.

The two Mario environments are `retro` (ROM execution with pixel observations)
and [mario_sim](../mario_sim/README.md) (CPU/CUDA simulation with semantic
observations, full-game episodes and FPG practice). The historical
[experiment register](EXPERIMENTS.md) preserves the earlier research record.

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
The active config loads the preserved 197/256-clear pixel policy for all-level
exploration and continues naturally through lives and level transitions.
Its original progress/new-area rewards are enabled. It uses the upstream
optimizer and rollout sampler. A checkpoint restores weights; optimizer,
schedules, recurrent state and the step counter start fresh.

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

The active command is the Retro-built `./puffer sweep`, using the single
editable `config/retro.ini`. The constant-rate search requests 500 successful
trials of exactly 50,331,648 additional decisions (192 rollouts) each. Every
trial independently loads `saved/retro/pixel_197_20261018.bin`, the completed
838.9M-decision continuation with 197/256 clears. Its total training lineage is
1,677,721,600 decisions. The parent SHA-256 is
`80127cad3ec19a5a1df84f5566a22a3c41d741a0bceec6da96ded2c9d95178bc`.
Weights are restored; optimizer and recurrent state reset for each candidate.

Explicit fixed bounds override inherited defaults for model/rollout/value settings.
The merged native config is tested to contain exactly eight varying dimensions.
Eight varying dimensions cover LR (5e-6–2e-4), entropy (0–.001), gamma
(.999–.99999), GAE (.99–.9999), replay (1–2.25), PPO clip (.1–.25), gradient
clip (.25–1), and momentum (.75–.95). LR and entropy annealing are disabled.
Trial zero uses LR 5.5e-5, approximately the average of the successful parent's
cosine schedule from 1e-4 to 1e-5, with its other PPO settings. Actors/horizon/
minibatch stay 1024/256/4096; threads/buffers stay 12/4. Model, observations,
reward weights, ROM and four-frame actions retain the successful run's settings.

These are early stability/improvement screens. Multiplying LR by the ratio of
long to short budgets does not reproduce on-policy exploration, gradient paths,
or long-run stability. The same constant-rate settings can be tested over 1B
decisions later; short-run survivors still require that longer validation.

Preflight rejects invalid sequence divisions, zero-update layouts, estimated
memory above 15,360 MiB and excessive graph construction work. Large actor counts
are useful only with horizons that fit the full pixel buffers. The env-owned
`sweep_worker.py` additionally measures resident VRAM and throughput, rejects
trials below 50K decisions/s after warmup, and limits startup to 600 seconds,
training to 30 minutes and final evaluation to 900 seconds. A runtime stop saves
and scores the actual trained budget; speed/memory failures enter Protein's
failure model. These controls add no PPO or ROM-rule changes.

The native GP history is `logs/retro_constant_20261019/sweep.history`.
Trial configs, progress CSVs, stderr and `.eval/trial.json` summaries are under
`logs/retro_constant_20261019/retro/`; final weights and their `.bin.panel.tsv`
reports are under `checkpoints/retro_constant_20261019/retro/`. The worker checks parent architecture metadata before spawning training and
forces final-only saves for sweep children. Each short trial saves once at completion. Ordinary 1B training saves every 192 epochs, about
50.3M decisions. The launch record, LR/update arithmetic and baseline are in
`reports/retro/constant_lr_20261019/`. The preceding broad search remains in
`reports/retro/pixel_rom_sweep_20261018/` with its original history.

Each final checkpoint is evaluated on exactly eight attempts per level, all
32 levels, fixed per-attempt sampling seeds, four-frame actions and a 30,000
native-frame budget. Natural lives/midpoint respawns remain enabled; each attempt
ends at its first source-level clear, game over or timeout. `panel` does not
balance training resets; it replaces the score sent back to Protein with a
balanced **evaluation** score. It is not a full-game or warp-route evaluation.

The active objective, `perf`, ranks level clears first, using
`100 * (clears + 0.5 * mean_bounded_progress) / attempts`. Any additional clear
outweighs the entire progress tie-break. Its score includes that small bonus;
use `panel/clear_rate` for the actual clear percentage. The preceding search's
`checkpoints` metric remains available and is retained in each report: mean
episode-local forward-frontier checkpoints (128 pixels), with revisits excluded.
Its distance cap can be reached without clearing a level. `speed` selects the
personal-best clear time; it does not rank by average completion time.

Every final model receives a `.bin.panel.tsv` report with level, replicate,
clear outcome, elapsed frames and progress checkpoints. Final INI metrics include
`panel/score`, `panel/clear_rate`, `panel/clear_frames`, `panel/attempts` and
`panel/checkpoints`. Ordinary training-window `env/*` metrics remain separate.
Protein receives one panel score and total train-plus-panel cost. The active
config uses `sweep.downsample=1`. The checkpoint's resolved config is passed to the evaluator;
the panel does not silently reread new training settings during a sweep.

Set `panel.repeats=0` to disable post-training evaluation for ordinary training.
The sweep worker requires a complete all-32-level panel. To choose another
parent, set `base.load_model_path` to one explicit preserved checkpoint and
keep its architecture fixed. Start a new history when changing the parent,
evaluation protocol or ranges. `sweep.trial_timesteps` sets the first trial's
budget; `[sweep.train.total_timesteps]` sets subsequent proposal bounds. The
active bounds are equal, fixing the same budget for every candidate.

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
standalone panel also supplies the native Protein post-training objective.

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

### Automatic ROM-state reset archive

`env.archive_enabled=1` collects exact QuickNES savestates during normal,
playable frames. It never edits ROM RAM or inserts resets into a live episode.
Cells distinguish level, loaded room, coarse position, power, and movement state;
occasional replacement varies enemy timing and other exact state within a cell.
Failure/no-progress feedback, underuse, and staleness bias sampling, with a
uniform floor. Coverage eviction removes a cell from the most represented level.
There is no authored obstacle sequence or mastery gate.

Each snapshot retains its screen and reward frontiers, visited areas and rewarded
level bits. Restoring cannot repay old novelty. The environment starts a fresh
episode and the trainer clears recurrent state through its ordinary terminal
mask; old policy hidden states are never restored. Episode-age observations,
local metrics, and clocks start fresh, so archive segments are not full-run RTA.

The baseline assigns actor lanes to 60% archive resets, 20% random level starts,
and 20% full 1-1 starts. Over 80% of the training budget it linearly changes to
10% archive / 90% full games. Lane changes apply at the next natural episode
boundary. These are target actor shares, not exact episode or decision shares;
`*.progress.csv` records actual level/full/archive decisions and schedule phase.
Episodes retain natural lives and level transitions, up to 108000 native frames
(about 30 minutes). The six reset parameters are the only varying sweep axes;
PPO is fixed to the strongest prior constant-rate candidate.

The bank has a 128 MiB payload budget. Active episodes retain shared snapshots
across bank eviction, so up to one additional snapshot per actor can remain
resident (roughly another 90 MiB at 1024 actors). Checkpoints write versioned,
ROM-fingerprinted, CRC-checked `.bin.archive.gz` files and complete `.bin.ini`
metadata. Loading a retained sidecar restores the bank and does not move the
schedule backward. Sweep workers retain archive sidecars for the best and most
recent completed trials, while preserving every policy, report and setting.
Each independent trial still starts from the same original parent.

The `curriculum` objective runs two fixed, seeded CPU evaluations with the
archive explicitly disabled: all 32 levels × 8 first-clear attempts, then 16
full games from 1-1 with natural lives and a 108000-frame cap. It is the existing
clear-first level-panel score plus `100 * full_game_win_rate + mean_unique_levels_cleared`.
The `.panel.tsv` and `.game.tsv` preserve both components and individual outcomes.
A full-game win requires the final ROM victory event; clearing 1-1 does not
terminate a full-game attempt. Use `sweep_eval --game --metric perf` for this
full-game evaluator. The ordinary level evaluator retains its 30000-frame cap.

Validation: `make -C ocean/retro archive-test test batch-test panel-test sweep-test`.
The archive test checks exact serialized ROM continuation, screen observations,
reward-ledger preservation, corruption/ROM rejection, reset schedule endpoints,
bounded eviction lifetime, and terminal-mask delivery. This is a Go-Explore-style
reset curriculum inside PPO, not a separate deterministic exploration phase or
trajectory-robustification implementation.
