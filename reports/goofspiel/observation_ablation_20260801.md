# Four-card Goofspiel observation ablation

Six paired seeds compare the compact current-state encoding with the
OpenSpiel-style one-hot encoding. All other settings are identical: native
64x2 PufferNet/MinGRU, Muon, EMAg, async collection, and a 24.576M-step training
budget. The learning rate was annealed over this budget, so these values should
not be mixed with runs whose annealing target was 32.768M.

## Population curves

| Steps | Compact mean | One-hot mean |
|---:|---:|---:|
| 1.638M | 0.729981 | 0.740833 |
| 3.277M | 0.663121 | 0.687238 |
| 4.915M | 0.535916 | 0.568477 |
| 6.554M | 0.374129 | 0.404395 |
| 8.192M | 0.240244 | 0.261036 |
| 9.830M | 0.152481 | 0.165940 |
| 11.469M | 0.097252 | 0.105363 |
| 13.107M | 0.063555 | 0.067586 |
| 14.746M | 0.043802 | 0.045741 |
| 16.384M | 0.031879 | 0.033528 |
| 18.022M | 0.025296 | 0.026578 |
| 19.661M | 0.023546 | **0.024039** |
| 21.299M | **0.023314** | 0.027205 |
| 22.938M | 0.026287 | 0.035614 |
| 24.576M | 0.036364 | 0.051273 |

The compact encoding is consistently faster to learn and has a slightly lower
fixed-step population minimum. Both encodings show the same late-training
degradation, so observation representation is not the source of that behavior.

## Per-seed oracle checkpoints

| Seed | Compact best | Step | One-hot best | Step |
|---:|---:|---:|---:|---:|
| 4001 | 0.017981 | 21.299M | 0.023186 | 19.661M |
| 4002 | 0.017369 | 21.299M | 0.027820 | 18.022M |
| 4003 | 0.019796 | 19.661M | 0.024358 | 19.661M |
| 4004 | 0.025559 | 19.661M | 0.021117 | 19.661M |
| 4005 | 0.014138 | 22.938M | 0.021498 | 21.299M |
| 4006 | 0.024383 | 22.938M | 0.022053 | 19.661M |
| Mean | **0.019871** | | 0.023339 | |
| SD | 0.004368 | | **0.002495** | |

Compact wins four of six paired oracle comparisons and achieves the lower
mean. One-hot is somewhat less variable, but does not improve the overall
result. Keep compact as the default; retain one-hot as a supported parity and
research option.

CPU and CUDA now call the same observation builder. Exact CPU/CUDA evaluation
agrees within approximately 2e-8 for both encodings.
