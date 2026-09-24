# Retro: full-screen SMB1

**Current experiment:** fixed return-pipe black-screen practice start.
See [PRACTICE.md](PRACTICE.md). Times are segment times, not full-run RTA;
`--full-run` in the viewer/evaluator restores the ordinary level start.

**NTSC migration (2026-09-20):** the active user-supplied cartridge is now
`roms/smb1_ntsc.nes`, SMB1 World/NTSC with a NES 2.0 header (SHA-1
`33d23c2f2cfa4c9efec87f7bc1321ce3ce6c89bd`). Its PRG/CHR exactly match the
[standard NTSC iNES release](https://tasvideos.org/1G#GameVersions), SHA-1
`ea343f4e445a9050d4b4fbac2c77d0693b1d0922`. Both verified wrappers are
accepted; PAL/modified images are rejected by the environment and block generator.
The old PAL ROM remains untouched at `roms/smb1.nes`. Its old timing logs are
**not comparable** to current NTSC results. See the [original audit](TIMING_AUDIT_20260920.md).

Retro runs the SMB1 ROM. The current default observation is 64×60 luminance (4×4 means of the native 256×240 screen), plus 112 RAM features: 3,952 inputs. `./build.sh retro` builds this resolution; `RETRO_OBS_SCALE=2 ./build.sh retro` selects 128×120. Resolution is compile-time, with no runtime dispatch. Different resolutions require separate checkpoints. Actions are held for `env.frameskip` native frames (currently one).

The current speed-reward experiment uses the user's 64×60 model and one-frame
actions. Rebuild it with `RETRO_OBS_SCALE=4 ./build.sh retro`. Its fresh outputs
go to `checkpoints/retro_cnn_64x60_pb`, separate from the pinned parent's
`retro_cnn_64x60_finetune` directory. See [FINETUNE.md](FINETUNE.md) for the current
fixed-parent sweep and winner-confirmation procedure.

### Speed-oriented rewards

The 1-1 configuration pays `completion_reward` (currently 0) plus
`completion_time_bonus * clamp((3000 - clear_frames) / 1800, 0, 1)`, all multiplied by
`reward_scale=0.0625`. The range is configurable through
`completion_time_min_frames=1200` and `completion_time_max_frames=3000`.
These are shaping anchors, not measured speed records. Inside the range,
each saved native frame adds `completion_time_bonus * reward_scale / 1800` reward; the timeout does not change
that slope. Both range options default to zero for the old budget-normalized
formula. Checkpoint, point, coin, idle and area rewards are zero in the current
config. Potential shaping has been removed; HUD timer extras are disabled, and
enabling them alongside RTA completion is rejected. Two optional 1-1 pipe
segment bonuses (currently disabled for fine-tuning) pay
`pipe_segment_bonus * clamp(1 - segment_frames/600, 0, 1)` before scaling.
They measure reset to first confirmed underground arrival, then underground
arrival to confirmed return to the initial overworld. Each pays once even
though the overworld destination was already visited. Weights and durations
are configured by `pipe_segment_bonus` and `pipe_segment_frames`.

`env/clear_frames` is mean NES frames per cleared level among completed
episodes (0 if none cleared). With `completion_on_rta_split=1`, the reward
uses the exact same event and frame count as the viewer/dashboard split:
playable reset through the next-level black-screen edge, including pipes,
flag descent, timer conversion and fireworks. It excludes the 1-2 entrance.
The final game win still completes on the ROM win event. Legacy modes remain:
with RTA completion disabled, `completion_on_next_playable=1` waits through
the next level's entrance, and both options zero selects first flag contact.
The two options cannot both be enabled. With `terminate_on_clear=1`, the
episode ends and pays on that exact frame, including inside a frameskip block. Continuing playback
does not earn a second clear at the following level load. `env/coin_events` reports positive
coin-counter increments since reset; `env/hud_coins` reports the ending ROM
counter including coins inherited from a snapshot. `env/idle_steps` counts idle
decisions, both averaged over completed episodes. `env/area_transitions`
reports confirmed same-level area loads (the generic signal for pipes, doors,
vines, and underwater entrances); `env/novel_areas` counts newly visited
destinations. Both are diagnostics, not reward payments. Generic area rewards
and their HUD timer extra have been deleted. Nonzero legacy area-reward
options are rejected. The former `area_transition_rewards` name was misleading
because it counted destinations even with the reward disabled.

## Normal use

```sh
OMP_WAIT_POLICY=PASSIVE ./puffer train retro
./retro watch latest --inspect
```

Both viewers show frame-derived NTSC RTA (`native frames / 60.0988138974405`), the last
level split and an average of completed attempts from the selected starting
level. Failure/timeout durations never dilute this average. The regular
viewer also shows the reward-finish time (identical in RTA completion mode).
The live clock is not a completed result; use the frozen **Finished** split
when comparing with the same split convention. Averages persist across
resets for the viewer session; training averages cover each logging window.

Timing follows [periwinkle's SMB1 LiveSplit autosplitter](https://github.com/periwinkle9/smb-autosplitter/blob/main/SuperMarioBros.asl):
start on entry into player control, normal level splits at the black-screen
timer edge, warp-zone splits at the next entrance, final finish on the 8-4
axe/victory event. The community's [SMB1 timing guide](https://www.speedrun.com/smb1/guides/frxdu)
starts RTA when TIME first appears in 1-1; title-screen time is excluded.
Reset preparation measures any offset between the autosplitter start and
the saved state (currently zero for all 32 prepared starts). The 1-1 reset is
byte-identical to an unmodified cold boot followed by START, including
CPU/PPU/APU state, RNG and the image. No RAM is written to prepare 1-1.
We still emulate the intro once, then restore that exact timed start; no
timed frames are skipped on reset. The old next-playable clock includes
additional 1-2 entrance time. Reference
timers that split at level start or add a reaction delay use different
boundaries. A later-level savestate begins a segment, not a full run from 1-1.

These are frame-derived simulation times: they include every
emulated frame, including lag and loading. Host pauses, fast-forward and
slow inference do not count. This permits training at any throughput; it
does not make a policy/savestate run a human leaderboard submission.

Dashboard and saved run-log fields (displayed before other environment statistics):

- `env/rta_seconds`: mean first RTA split in seconds, completed attempts only.
- `env/rta_frames`: the same conditional mean in native frames.
- `env/rta_clear_rate`: fraction of ended episodes reaching that split.
- `env/rta_valid`: 1 with the verified NTSC ROM/core; this is not leaderboard
  eligibility. Old `rta_seconds` logs from the PAL ROM are invalid comparisons.
- `env/clear_seconds`: mean time to the configured reward-finish endpoint;
  equal to `rta_seconds` for single-clear training in split completion mode.

With no successful splits the numeric mean is zero and the rate is zero;
the viewer shows `--`. Do not interpret that as a zero-time completion.
Training aggregates sums/counts across environments before dividing, so
lanes with different completion counts have the correct weight. Use a single
starting level when comparing policies' splits. The update is
a few scalar operations per frame plus a division when logging, with no
extra policy inference or frame rendering. `completion_on_rta_split` opts
the reward and terminal boundary into this same timing event.

For a reproducible headless split trace (argmax policy; requires frameskip 1):

```sh
./retro watch latest --timing
```

This prints CSV at pipe-entry routines, playable area arrivals, flag contact,
next-level load, `rta_split`, and the selected clear endpoint.
It includes native frames/RTA seconds, HUD TIME, the HUD countdown
divider, the interval counter, reward, and both total and rewarded area events.
`nes_frames` comes from the emulator's own video scheduler, checked against
the wrapper frame count on every step of the audit.
It uses the current viewer config, reports the selected checkpoint/config,
and stops at the RTA split (next playable level in that legacy mode), death,
or 6000 frames. The interval counter
alone is not a prediction of the next transition: other ROM state also gates
it. Compare the same event when using a reference run. HUD TIME pauses during
animations and cannot be converted to total frames by a fixed multiplier.
NTSC active play is tested at 24 frames per HUD unit and 21 frames per
interval-counter cycle; they are separate clocks.

Config uses `load_model_path = latest` to continue training on 1-1, with checkpoints under `checkpoints/retro_cnn_64x60_speed/retro`. Training and watch read the same directory; `watch latest` does not fall back to older folders. For a fresh run use `base.load_model_path=None`. Warm starts do not restore optimizer state. Graceful Ctrl-C saves at the next safe boundary; SIGKILL cannot save. Running trainers/viewers retain their startup config and binary: restart them after changes.

Existing checkpoints were trained with PAL game physics/timers. Their shapes
still load, but NTSC performance must be remeasured and may need fine-tuning.
To evaluate an old checkpoint on the new ROM, pass `--config config/retro.ini`
to `sweep_eval`; its old sidecar otherwise selects the now-rejected PAL ROM.
Do not compare archived PAL clear times with the new NTSC measurements.

For a speed sweep on the configured starts (currently 1-1):

```sh
OMP_WAIT_POLICY=PASSIVE ./puffer sweep retro
```

`speed` ranks clear count first and mean native frames per clear second; when
no attempts clear, forward progress breaks ties. It uses the configured
completion endpoint, matching the completion reward timing.
`distance` selects mean nonnegative forward pixels;
`perf` (alias `score`) ranks clears then bounded progress. The console's
generic `score=` field contains the selected objective, not shaped reward.
Each checkpoint's panel respects its recorded `spawn_levels` and `frameskip`.
Reports include those settings, clear rate and mean `clear_frames`.

`train.reward_clip=0` disables clipping and is supported in sweeps. A positive
clip retains the conservative reward-bound check. Use `base.load_model_path=None`
for fresh trials or a concrete checkpoint path for a fixed warm start; moving
`latest` is rejected. Each selected parameter's current value must lie inside
its range because the first trial runs the current defaults.
For a separate all-level panel, pass `--levels all` to `sweep_eval`; archived
v1 panels always used all 32 starts and frameskip 1. See [SWEEP.md](SWEEP.md).

The inspector starts paused and displays the actual policy input beside the original NES image, all RAM features, controls and a pixel-error check. P/Space pauses, N advances one decision, R resets, 1/2/3 selects speed, and F12 saves a screenshot and observation CSV. To inspect without a trained policy: `./retro play 1-1 --inspect` (arrows and X/Z).

Select the watch starting level with `--level` or a positional level:

```sh
./retro watch latest --level 4-2
./retro watch latest 8-4 --inspect
./retro watch latest --random --inspect
```

Watch and play now continue through the ROM's normal death animation and
remaining lives, including its own respawn/checkpoint behavior. Dying after
advancing a level does not send the run back to the selected starting level.
Game over, beating the final castle, or pressing R starts a new game at the
selected level (`--random` chooses another start). There is no training-length
timeout in normal playback. Recurrent policy state clears on the new life's
first playable frame; the emulator is not reset then. `--continue` remains
accepted for compatibility; continuation is the default.

Use `--single-life` to watch the previous training-style death/timeout resets.
Training and sweep evaluation still use those single-life episode boundaries;
this viewer change does not alter the model, checkpoints, or training config.
`--inspect-check` intentionally keeps its short episode limit for headless
reset/image regression tests.

CPU-only watch regression checks: `make -C ocean/retro test` covers natural
respawns against raw ROM execution, including game over and single-life
training compatibility. `python3 ocean/retro/tests/test_watch_cli.py ./retro
PATH.bin` checks level selection, both viewer paths, and invalid options with
a compatible checkpoint; it never opens a window or writes that checkpoint.

## Build and validate

```sh
./build.sh retro
./build.sh retro --encoder-test
./test_retro_encoder
make -C ocean/retro/batch test
OMP_WAIT_POLICY=PASSIVE python3 tests/test_native_retro_runtime.py ./puffer
```

The normal native build produces `./puffer` and the matching `./retro` viewer, including the inspector. Both are compiled before either is replaced, and replacement leaves an already-running trainer on its original executable. `HEADLESS=1` skips the viewer. `NATIVE_OUTPUT_NAME=puffer_retro_cnn_candidate ./build.sh retro` builds an isolated trainer without replacing either normal entry point; `OUTPUT_NAME=retro_candidate ./build.sh retro --fast` does the same for the viewer. There is no launch wrapper or automatic fresh/latest switching.

The config uses asynchronous rollouts, compiled ROM CPU blocks, full-screen wide rendering, and two 128-wide recurrent layers. All 32 level starts are supported; the current training start is 1-1.

By default training plays through flags into the next level until death, the final win, or `env.max_frames`. Set `env.terminate_on_clear=1` (current `config/retro.ini` default) to end the episode at the first flag/castle clear instead, which matches the sweep panel. Set it back to `env.terminate_on_clear=0` to keep the old continue-through behavior. The viewer keeps natural-life continuation unless `--single-life` is passed. Agent, worker, horizon, and minibatch settings remain experiment-specific in `config/retro.ini`.

## Network

The image and RAM take separate learned paths:

```text
64x60 luma   -> Conv8(8x8,s4) -> Conv16(4x4,s2,p1) -> Conv32(3x3,s2,p1) -> 512 features
112 RAM     -> Linear112->32                                                      -> 32 features
                                   concatenate -> Linear544->128 -> 2x MinGRU128
                                                                  -> 64 action logits + value
```

All convolutions, the RAM projection, and the fusion projection have biases and ReLU activations. Padding/strides preserve coverage of every image pixel, including all four edges. The default network has 187,224 parameters (748,896-byte float32 checkpoint). Dense and 128×120 weights are incompatible. The original 128×120 checkpoint and executables are bookmarked in `artifacts/retro-128x120-20260916/README.md`.

CUDA training and CPU watch/panel evaluation share the architecture constants and checkpoint layout. Tests cover intermediate convolution maps, full recurrent logits/value through resets, both observation branches, every image edge, and numerical gradients for all ten encoder parameter tensors. `RETRO_TEST_BF16=1 ./build.sh retro --encoder-test` builds the production-precision parity test; the default test uses float32 for finite differences.

Observation construction reads the emulator's native 256×240 palette-index
image directly into the observation, without an upscaled RGB frame.
On AVX2 hosts the native build produces eight output pixels together, avoiding
the full float staging image; other hosts retain the original staged builder.
Box averages use row-major addition and final float32/bf16 rounding.
Rendering in the core still uses native NES dimensions; no additional
emulator resolution change or frame skipping was introduced.

The CUDA encoder borrows the read-only minibatch input through backward,
removing its redundant saved-input allocation and device copy (15.4375 MiB
at minibatch 2,048 in bf16). The caller must retain that input until backward
finishes. Native training satisfies this on its training stream, including
CUDA-graph replay. PufferLib's rollout transpose, timing events, and cuBLAS
workspace management are unchanged. The encoder test compares borrowing
against owned copies, including all gradients and replay with changing data.

The CUDA convolutions use fused forward/bias/ReLU kernels and direct input
gradients. Weight/bias gradients use a tiled split reduction with float partials,
avoiding the large temporary image-patch matrix. This is an execution change,
not a new architecture: existing 351,064-parameter CNN checkpoints load without
conversion, and the CPU viewer is unchanged. The fused path preserves the
original bf16 rounding points; gradient reductions can differ by normal
floating-point summation error.

For additional validation on an idle GPU, run
`./test_retro_encoder --large --checkpoint PATH.bin`. This checks all three
convolutions against the original CUDA implementation at the full 2,048-sample
minibatch, then compares both complete recurrent policies on actual ROM frames
using the supplied checkpoint. It does not modify that checkpoint.
`RETRO_CNN_FUSED=0 NATIVE_OUTPUT_NAME=build/retro_reference ./build.sh retro`
builds the original execution path for comparisons without replacing the normal
trainer/viewer. This switch does not change the model or observation contract.

`python3 ocean/retro/batch/benchmark_cnn.py BASELINE CANDIDATE CHECKPOINT --buffers 1`
runs a matched ABBA real-PPO comparison with the same checkpoint and settings.
It writes only to a private temporary directory, never stops other processes,
and must run on an idle GPU. Startup and final asynchronous-drain updates are
excluded from its reported rate. More buffers divide inference into smaller
batches and add per-worker resources; they are not automatically faster.
The [2026-09-16 matched CNN results](batch/RESULTS_20260916.md) measured
3.3–3.5x real-PPO throughput on the GTX 1060 3 GB without changing the model.
The [observation-copy follow-up](batch/RESULTS_20260916_OBSERVATIONS.md)
documents the additional input-storage/SIMD changes and full PPO parity checks.

Earlier 7–9K decisions/s measurements describe the dense full-screen policy, not this CNN. Throughput depends on convolution and activation costs as well as parameter count; fewer parameters is not a speed guarantee. The emulator remains on CPU; PPO and training-time policy inference use CUDA. The watch command runs the matching policy on CPU.

Tests compare the compiled CPU and reference interpreter, complete rendered frames, all downsampled pixels, rewards, terminal/reset behavior, and actual PPO checkpoint save/reload. Explicit crop rendering settings and wrong-size checkpoints fail rather than silently changing what the policy sees.
