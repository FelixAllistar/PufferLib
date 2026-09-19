# Retro: full-screen SMB1

Retro runs the SMB1 ROM. The current default observation is 64×60 luminance (4×4 means of the native 256×240 screen), plus 112 RAM features: 3,952 inputs. `./build.sh retro` builds this resolution; `RETRO_OBS_SCALE=2 ./build.sh retro` selects 128×120. Resolution is compile-time, with no runtime dispatch. Different resolutions require separate checkpoints. Actions are held for `env.frameskip` native frames (currently four).

The current speed-reward experiment uses the user's 64×60 model and four-frame
actions. Rebuild it with `RETRO_OBS_SCALE=4 ./build.sh retro`. Its fresh outputs
go to `checkpoints/retro_cnn_64x60_speed`, separate from earlier point-reward
runs (some of which used 64×60 despite their directory name).

### Speed-oriented rewards

Current config rewards new 128-pixel frontier checkpoints (+0.0078125), a new
level clear (+0.625), and an additional `0.3125 * max(0, 1 - clear_frames/3000)`
on that clear. Each newly collected coin pays +0.00625; Mario points remain
disabled. After eight decisions with no X movement, coin pickup, level advance,
or terminal event, an idle decision costs 0.00125. Existing potential-based
shaping remains, and all reward terms share `reward_scale`. The idle charge is
per decision (not per native frame), so it also works predictably with
`frameskip=4`. `completion_time_bonus=0`, `coin_reward=0`, and
`idle_penalty=0` restore the corresponding old terms.

`env/clear_frames` is mean NES frames per cleared level among completed
episodes (0 if none cleared), measured between level transitions. It includes
the ROM's flag/transition sequence but excludes subsequent waiting after the
clear. Read it alongside clear rate and deaths: episode length alone includes
time spent after a successful clear. No episode, emulator or playback boundary
was changed by these reward adjustments. `env/coin_events` reports positive
coin-counter increments, while `env/idle_steps` reports charged anti-stall
decisions, both averaged over completed episodes. `env/area_transitions`
reports confirmed same-level area loads (the generic signal for pipes, doors,
vines, and underwater entrances); `env/area_transition_rewards` reports how
many of those destinations were novel and reward-eligible. The config leaves
`area_transition_reward=0` while this signal is inspected; set, for example,
`env.area_transition_reward=0.05` to enable a small raw reward once per novel
destination per episode.

## Normal use

```sh
OMP_WAIT_POLICY=PASSIVE ./puffer train retro
./retro watch latest --inspect
```

Config uses `load_model_path = None` for fresh training on 1-1, with checkpoints under `checkpoints/retro_cnn_64x60_speed/retro`. Training and watch read the same directory; `watch latest` does not fall back to older folders. To warm-start compatible weights, explicitly set `base.load_model_path=latest`. This does not restore optimizer state. Graceful Ctrl-C saves at the next safe boundary; SIGKILL cannot save.

For a speed sweep on the configured starts (currently 1-1):

```sh
OMP_WAIT_POLICY=PASSIVE ./puffer sweep retro
```

`speed` ranks clear count first and mean native frames per clear second; when
no attempts clear, forward progress breaks ties. It uses the level transition
time, including the flag/countdown/fireworks sequence, just like the current
completion time bonus. `distance` selects mean nonnegative forward pixels;
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

The config uses asynchronous rollouts, compiled ROM CPU blocks, full-screen wide rendering, and two 128-wide recurrent layers. All 32 level starts are supported; the current training start is 1-1. Agent, worker, horizon, and minibatch settings remain experiment-specific in `config/retro.ini`.

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
