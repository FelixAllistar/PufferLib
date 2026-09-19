# Before 64x60 cutover

Latest completed checkpoint bookmarked on September 16, 2026 at approximately
12:51 EDT: `0000000020971520.bin` (20,971,520 decisions).
Source: `checkpoints/retro_cnn_fresh/retro/1789576434667/`.
SHA256: `9784d75b18dcf4b714f8ab7b0a58a5d32b9843f620fbd8c5f926db59ac7d9f1e`.

Contract: fullscreen128x120_luma_v1, 15,472 inputs, hidden 128, two layers,
351,064 weights. Copied checkpoint sidecar, original config, trainer and viewer
are alongside this file. Run the archived viewer from the repository root with
the explicit archived checkpoint path. The original training run was still
active when this bookmark was made, so later checkpoints may also exist.

The new default is 64x60 (3,952 inputs, 187,224 weights), incompatible with
these weights. Fresh runs go to `checkpoints/retro_cnn_64x60`.
Build using `./build.sh retro`; restore the old architecture using
`RETRO_OBS_SCALE=2 ./build.sh retro` (and explicitly choose the old checkpoint
directory when resuming). Resolution is compile-time, not a runtime INI option.

Validation completed: CPU reference and compiled-ROM suites; full-resolution
CPU compatibility suite; float32 and bf16 encoder/gradient parity (including
batch 2048 and CUDA graph replay); a 4,096-decision fresh PPO smoke test with
checkpoint save and reload across all 32 levels; and 600-decision inspector
checks for both the archived old model and the new smoke-test model. No
multi-seed learning runs or learning-quality comparisons were performed.

To watch this bookmark from the repository root:

```sh
artifacts/retro-128x120-20260916/retro watch artifacts/retro-128x120-20260916/0000000020971520.bin --inspect
```
