# Retro observation copy elision — 2026-09-16

This follow-up preserves all 15,472 inputs, the 128×120 image, 351,064 policy
parameters, one-frame controls, emulator behavior, rewards and PPO settings.
It changes only our Retro implementation and its tests/build support.

## Scope and provenance

The per-step CUDA timing events and cuBLAS workspace management predate our
Retro integration: `git blame` attributes the relevant code to upstream
commits `2e10228747`, `c908f2e875`, and `4ec2c4f56f`. The full rollout-to-train
observation transpose is also upstream. `src/pufferl.cu`, `src/algo.cu`, and
the shared substrate were not changed in this follow-up.

The encoder's saved-input allocation belongs to our Retro encoder. Native
training already retains the minibatch input until backward completes, with
the next minibatch selection ordered afterward on the same CUDA stream.
Borrowing that input removes one device copy per minibatch and **60.4375 MiB**
of allocated GPU storage at minibatch 2,048 in bf16. Parameter registration
and checkpoint layout do not change.

## Pixel construction, without reducing resolution

The core generates the NES-native 256×240 palette-index frame, not an
upscaled RGB image. The observation builder retains the existing four-pixel
mean to produce 128×120 luminance. Native rendering still runs at NES
resolution; this change does not pretend that lowering the observation size
would automatically make emulation cheaper.

AVX2 builds now gather and average eight output pixels together and write
their final float32/bf16 representation directly. Addition order and bf16
round-to-nearest-even are preserved. This removes the full float staging
image on the tested host. Non-AVX2 builds keep the original staged algorithm.
The encoder-test build uses the same native CPU instruction selection as
the trainer, so the production host path is actually exercised.

An initial scalar direct-output version was rejected: it cost about
39.6 microseconds per observation versus 30.0 for the original. A row-sized
staging variant was also slower. The retained explicit SIMD implementation
measured **30.24 / 16.99 / 17.46 / 29.24 microseconds** in old/new/new/old
order (20,000 iterations per run, real ROM frame, production bf16,
i5-7400, native host compilation). Means are 29.74 versus 17.23 microseconds,
about 42% less observation-builder time. These are not full environment-step
or end-to-end training timings.

Isolated CUDA-graph encoder runs confirmed activation/input storage dropping
from 236.817 to 176.379 MiB. Two alternating runs per implementation averaged
6.15 versus 4.69 ms for forward, and 35.76 versus 34.58 ms for combined
forward/backward at batch 2,048. These short measurements fluctuate with
clock/load and are not an end-to-end speed guarantee.

## Matched end-to-end PPO throughput

GTX 1060 3 GB / i5-7400, 128 agents, four buffers/workers, minibatch 2,048,
horizon 128, replay ratio 1, bf16, CUDA graphs, asynchronous training,
compiled ROM CPU, full-screen wide rendering, seed 73, blocking workers and
`OMP_WAIT_POLICY=PASSIVE`. Both binaries loaded the same checkpoint and
inherited the same remaining config. No compiler or other test job ran
alongside these measurements.

Each run used 24 updates, excluding the first two warm-up updates and final
drain from timing. ABBA followed by BAAB gave four runs per implementation;
rates below are total measured decisions divided by total measured seconds.

| Implementation | Measured decisions | Seconds | Decisions/s |
| --- | ---: | ---: | ---: |
| Before this follow-up | 1,376,256 | 69.0253 | 19,938 |
| Borrowed input + SIMD observations | 1,376,256 | 63.3587 | 21,722 |

**8.94% higher throughput** across the combined comparison. The two ordering
groups separately improved by 9.26% and 8.63%. Individual original runs
ranged from 18,770 to 21,277 SPS; optimized runs from 20,213 to 23,309 SPS.
These short tests demonstrate a modest gain under matched conditions, not
a guaranteed multiplier on a live dashboard's instantaneous SPS.

Raw logs/commands: `/tmp/retro-cnn-abba.gifxabzj/` and
`/tmp/retro-cnn-abba.x36ceqn2/`. In the second directory the binary arguments
were deliberately swapped: its `baseline` label is the optimized binary.

## Correctness

- 5,120 float32 observations through vector stepping matched the staged
  builder bit-for-bit, along with reset observations at all 32 starts.
- Synthetic palette tests exercise all 256 indices and 16 palette/emphasis
  patterns in float32 and production bf16.
- Borrowed-input versus owned-copy encoder outputs and all weight/bias
  gradients matched exactly at batches 3 and 2,048, including pointer changes
  and three CUDA-graph replays with overwritten minibatch contents.
- The learned 36,888,576-decision checkpoint produced identical bf16
  logits/values and action probabilities through the original and fused
  convolution policies on 384 real ROM frames with six recurrent resets.
- Six complete synchronous PPO updates produced bit-identical saved weights
  before/after these changes, both with CUDA graphs disabled and enabled.
  All 351,064 parameters were finite and the checkpoint changed from its
  initial weights. Final SHA-256 in both modes:
  `d517615a580be5da7a0ae3e0031f8586a3e1c9c29d6dc40a959e3c828ed52e10`.
- The 65,536-frame ROM parity suite, reset/reward/vector checks, and natural
  playback/life tests passed.
- The normally installed `./puffer` independently repeated the exact PPO
  weight comparison and passed synchronous SIGINT/asynchronous SIGTERM
  save-and-reload tests over all 32 levels. The rebuilt `./retro` passed the
  headless viewer/inspector CLI suite. Training was not restarted.

The earlier bf16 checkpoint test harness had incorrectly provided a float
host array to an environment emitting packed bf16 observations. This was a
test-only packing bug, not a trainer bug. It is corrected here; the results
above use correctly typed host observations. The earlier test's claim to
validate real production-precision ROM input should be superseded by these
corrected checks.

## Reproduction and artifacts

Save the pre-change executable, build the candidate to a separate output,
and run with no competing training/compiler jobs:

```bash
RETRO_TEST_BF16=1 ./build.sh retro --encoder-test
./test_retro_encoder --large --checkpoint PATH_TO_CNN_CHECKPOINT.bin
python3 tests/test_retro_copy_elision.py BASELINE CANDIDATE CHECKPOINT
python3 ocean/retro/batch/benchmark_cnn.py BASELINE CANDIDATE CHECKPOINT \
  --agents 128 --buffers 4 --replay-ratio 1 --updates 24
```

The benchmark fixes its replay ratio explicitly instead of inheriting a
possibly edited config. GPU/CPU component benchmark sources are
`tests/bench_retro_encoder.cu` and `tests/bench_retro_observation.cu`.

Local temporary artifacts: `/tmp/retro-obs-reuse.sNFnvA/` contains original
executables/config, the read-only checkpoint copy, candidate binaries and
test/build logs. Final PPO parity artifacts: `/tmp/retro-copy-parity.1ho_n2fy/`.
Published-binary checks: `/tmp/retro-copy-parity.f3n_5zaa/` and
`/tmp/retro-runtime-test._dhew6nb/`. The user's config and source checkpoint
were byte-compared against their pre-change copies after installation.
The input checkpoint SHA-256 remained
`52cc34eb172120d5c45ef6128a027cb56466e6474150ad8ad3d3cd2523fd82e1`.
