# Retro: full-screen SMB1

Retro runs the SMB1 ROM with one-frame controls. The policy always sees the entire 256×240 NES image downsampled to 128×120 luminance using 2×2 averages, plus 112 RAM features: 15,472 inputs total. There is no crop mode or configurable observation contract.

## Normal use

```sh
OMP_WAIT_POLICY=PASSIVE ./puffer train retro
./retro watch latest --inspect
```

Config defaults to `load_model_path = None` for fresh CNN training. Checkpoints live under `checkpoints/retro_cnn_fresh/retro`, separate from the preserved earlier CNN and dense/crop runs. Training and watch read the same checkpoint location from the config; `watch latest` requires a checkpoint there and never falls back to the older folders. To continue a saved CNN policy later, explicitly use `./puffer train retro base.load_model_path=latest`. Loading weights is a warm start, not an exact optimizer-state resume. Graceful Ctrl-C saves at the next safe boundary; SIGKILL cannot save. With the default save interval, the first periodic checkpoint appears at 1,310,720 decisions.

The inspector starts paused and displays the actual policy input beside the original NES image, all RAM features, controls and a pixel-error check. P/Space pauses, N advances one decision, R resets, 1/2/3 selects speed, and F12 saves a screenshot and observation CSV. To inspect without a trained policy: `./retro play 1-1 --inspect` (arrows and X/Z).

## Build and validate

```sh
./build.sh retro
./build.sh retro --encoder-test
./test_retro_encoder
make -C ocean/retro/batch test
OMP_WAIT_POLICY=PASSIVE python3 tests/test_native_retro_runtime.py ./puffer
```

The normal native build produces `./puffer` and the matching `./retro` viewer, including the inspector. Both are compiled before either is replaced, and replacement leaves an already-running trainer on its original executable. `HEADLESS=1` skips the viewer. `NATIVE_OUTPUT_NAME=puffer_retro_cnn_candidate ./build.sh retro` builds an isolated trainer without replacing either normal entry point; `OUTPUT_NAME=retro_candidate ./build.sh retro --fast` does the same for the viewer. There is no launch wrapper or automatic fresh/latest switching.

Defaults in `config/retro.ini`: 64 environments, four blocking workers, asynchronous rollouts, compiled ROM CPU blocks, full-screen wide rendering, and two 128-wide recurrent layers. All 32 level starts are supported; the default training start is 1-1.

## Network

The image and RAM take separate learned paths:

```text
128x120 luma -> Conv8(8x8,s4) -> Conv16(4x4,s2,p1) -> Conv32(3x3,s2,p1) -> 1792 features
112 RAM     -> Linear112->32                                                      -> 32 features
                                   concatenate -> Linear1824->128 -> 2x MinGRU128
                                                                  -> 64 action logits + value
```

All convolutions, the RAM projection, and the fusion projection have biases and ReLU activations. Padding/strides preserve coverage of every image pixel, including all four edges. The default network has 351,064 parameters (1,404,256-byte float32 checkpoint). The decoder and recurrent architecture are unchanged; old dense weights cannot initialize the CNN.

CUDA training and CPU watch/panel evaluation share the architecture constants and checkpoint layout. Tests cover intermediate convolution maps, full recurrent logits/value through resets, both observation branches, every image edge, and numerical gradients for all ten encoder parameter tensors. `RETRO_TEST_BF16=1 ./build.sh retro --encoder-test` builds the production-precision parity test; the default test uses float32 for finite differences.

Earlier 7–9K decisions/s measurements describe the dense full-screen policy, not this CNN. Throughput depends on convolution and activation costs as well as parameter count; fewer parameters is not a speed guarantee. The emulator remains on CPU; PPO and training-time policy inference use CUDA. The watch command runs the matching policy on CPU.

Tests compare the compiled CPU and reference interpreter, complete rendered frames, all downsampled pixels, rewards, terminal/reset behavior, and actual PPO checkpoint save/reload. Explicit crop rendering settings and wrong-size checkpoints fail rather than silently changing what the policy sees.
