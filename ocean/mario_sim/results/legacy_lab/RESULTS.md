# First qualification — 2026-10-01

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

The CUDA environment runs, trains, saves checkpoints and replays evaluated action
tapes. The first mixed policy handles several obstacle families but fails gaps.
These are synthetic-controller results. At this qualification stage, real Mario
transfer had not been tested.

This historical qualification predates the [controller calibration and real
1-1 transfer results](TRANSFER_RESULTS.md). Its original runs and metrics remain
unchanged.

## Correctness

`make -C ocean/mario_lab test sanitize gpu-test` passes controller, collision,
pipe, enemy, observation-invariance, snapshot and 800-layout generation checks.
CUDA parity covers 17 environments over 256 decisions in each of three cases,
including non-default streams, CUDA graph execution, room transitions, clears,
deaths, timeouts and automatic resets. Integer game states match exactly;
floating rewards, observations and logs are compared with numerical tolerances.

The viewer builds, and its headless script/JSONL trace modes were exercised.
The graphical window was not opened. Native GPU training and CPU checkpoint
loading were exercised. All three saved final checkpoints have 100,928 finite
FP32 parameters; training logs contain finite losses.

## Throughput

Hardware: NVIDIA GeForce GTX 1060 3GB, driver 581.63. One decision is one frame.

| Workload | Measurement |
| --- | --- |
| Simulator benchmark, 4,096 environments × 4,096 frames | 16,777,216 frames in 5.320 seconds: **3.15 million frames/s** |
| Flat training, 128 environments, 64-unit/two-layer policy | 10,485,760 frames in 39.456 wall seconds including startup: **266k frames/s** |
| Mixed continuation, same policy size | 10,485,760 frames in 29.063 wall seconds including startup: **361k frames/s** |

The simulator benchmark includes scripted actions, observation production and
automatic resets, with 64 warmup steps excluded from the timer. It excludes
policy inference and PPO. Reset/clear counts in its JSON include warmup.
Training timings include the launcher-to-trainer process lifetime and exclude
the subsequent evaluation. Initial CUDA startup/cache costs differ across runs;
these single runs are qualification measurements, not a scaling study.

Exact benchmark inputs, hashes and output are in [benchmark.json](results/qualification_20261001/benchmark.json).
Training settings, identities, timings and panel summaries are in [training.json](results/qualification_20261001/training.json).

## Learning

All runs use the MLP/MinGRU baseline, H=64, L=2, 128 environments, one-frame
decisions and default reward coefficients. Training seed is 73. Geometry is
generated on reset; dynamics stay fixed. These are single-seed runs.

1. **Flat smoke:** 1,048,576 training frames, length 48. Native CUDA reload check:
   1/128 clears. This tests the pipeline and is not a useful policy.
2. **Flat baseline:** 10,485,760 frames from scratch, length 48. CUDA reload check:
   512/512 clears. Complete-episode CPU validation: 32/32 clears, mean 466.8 frames.
   Flat layouts have identical geometry across seeds; this is a movement result.
3. **Mixed continuation:** a further 10,485,760 frames from the flat checkpoint,
   length 64, difficulty 1, mixed course sampling. Optimizer state starts fresh.
   Native CUDA reload check: 367/514 clears. It can censor slow unfinished
   episodes; the complete-episode panel below is the family comparison.

The mixed policy's CPU panel uses validation split 1, seed 901, 32 fully finished
episodes per family, independent recurrent resets and recorded stochastic action
seeds. Every action tape reproduces the full final simulator state exactly.

| Family | Clears | Mean clear frames |
| --- | --- | --- |
| Flat | 32/32 | 616.8 |
| Gaps | **0/32** | — |
| Pipe obstacles | 32/32 | 673.9 |
| Stairs | 32/32 | 693.1 |
| Walkers | 18/32 | 614.0 |
| Required underground route | 16/32 | 1,193.4 |
| Composite | 27/32 | 975.7 |

The evaluator's simulator core matches the mixed training source snapshot. The
earlier flat run used the version before the underground side-exit contact fix;
the evaluator manifest records that mismatch. Flat geometry does not use exits.
These validation seeds are now development evidence; split 2 remains unused.
The generated family coverage is small, and no exhaustive solvability claim is
made. Mean clear time excludes failures and must be read alongside clear rate.

## Artifacts and next experiment

Local run bundles contain captured binaries/sources/configs, console logs,
checkpoints and evaluation tapes:

- [Flat smoke manifest](../../logs/mario_lab/experiments/CUDA001_flat-smoke_20261001T091305Z_9677b8ff/manifest.json)
- [Flat baseline manifest](../../logs/mario_lab/experiments/CUDA001_flat-baseline_20261001T091415Z_00856c7b/manifest.json)
- [Mixed continuation manifest](../../logs/mario_lab/experiments/CUDA001_mixed-from-flat_20261001T091823Z_559054b8/manifest.json)
- [Complete mixed family panel](../../logs/mario_lab/experiments/CUDA001_mixed-from-flat_20261001T091823Z_559054b8/evaluations/20261001T092051Z_088c58ee/manifest.json)

The follow-up [curriculum results](CURRICULUM_RESULTS.md) cover gap takeoff/landing,
pipe entry/recovery, reset-only comparisons and adaptive mixed training. Root-start
evaluation measures skill retention. Acceleration, braking and held/released jumps
still need calibration before real 1-1 transfer. The broader
[experiment register](../retro/EXPERIMENTS.md) tracks TAS reset data, QD,
interpretability and Go-Explore on both CUDA and ROM backends.
