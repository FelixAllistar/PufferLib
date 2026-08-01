# Four-card Goofspiel wall-clock/capacity audit

All corrected EMAg runs use Muon, CUDA graphs, asynchronous collection, the
egocentric compact observation, and exact best-response scoring.

## Fast low-update sanity checks

The original 32x1 schedule reached approximately 3M SPS but barely learned:
plain PPO moved from 0.75384 to 0.74822 exploitability over 10M steps. Restoring
EMAg under that schedule ended at 0.75363 because only about 38 rollout epochs
occurred and the slow EMA barely moved.

## Efficient large-rollout schedule

| Model | SPS | Best EMA exploitability | Best step |
|---|---:|---:|---:|
| 32x1 | 820K | 0.047541 | 72.09M |
| 32x2 | 754K | 0.053493 | 65.54M |
| 64x1 | 759K | 0.069320 | 58.98M |

The first 40-run wall-clock sweep found a short-run Pareto point at 0.281644
exploitability in 15.74 seconds (866K SPS). A 1.82M-SPS candidate reached only
0.490900. Extending the best efficient 32x1 configuration produced the much
better 0.047541 checkpoint above, then degraded after approximately 85M steps.

## Controlled 32K rollout batch

| Model | Full wall time | Best EMA exploitability |
|---|---:|---:|
| 32x1 | 91.21 s | 0.091435 |
| 64x2 | 132.61 s | 0.035522 |
| 128x1 | 137.32 s | 0.046391 |

Each run used approximately 1,000 policy refreshes and 16,000 optimizer
minibatches. Moderate width plus depth was substantially better than width
alone.

## High-frequency 16K rollout batch

| Model | Full budget | Full wall time | Best EMA exploitability | Best step |
|---|---:|---:|---:|---:|
| 32x1 | 64M | 216.66 s | 0.088200 | 36.04M |
| 64x2 | 64M | 288.93 s | **0.009427** | 32.77M |
| 64x3 | 40M | 373.83 s | 0.012893 | 27.85M |
| 128x2 | 40M | 288.33 s | 0.014553 | 29.49M |

The 64x2 checkpoint at 32.768M is the best observed policy:

`checkpoints/goofspiel/goofspiel4_highupdate_64x2_64m/0000000032768000.bin.emag`

Every EMA trajectory eventually degraded. Exact checkpoint selection is
therefore important; final checkpoints from arbitrarily long runs are not valid
comparisons.
