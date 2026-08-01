# EMAg parity check — 2026-07-30

Configuration: four-card Goofspiel, synchronous PPO, 64x2 MinGRU, 2048 agents,
horizon 8, minibatch 4096, replay ratio 4.43, learning rate 0.00860058703,
GAE lambda 0.990712583, clip 0.0870083869, value coefficient 0.0647966638,
entropy coefficient 0.0450635739, EMAg KL 0.163094044, EMAg tau 0.00957633369.
Each run was trained for 3,686,400 agent steps and scored with the exact CUDA
best-response solver on the final `.emag` checkpoint.

## 24-seed replication

| Seed | Exploitability |
|---:|---:|
| 1 | 0.047503019 |
| 2 | 0.048978520 |
| 3 | 0.047495264 |
| 4 | 0.044706443 |
| 5 | 0.070988802 |
| 6 | 0.046044904 |
| 7 | 0.050423390 |
| 8 | 0.064120463 |
| 9 | 0.055847339 |
| 10 | 0.061370194 |
| 11 | 0.061962423 |
| 12 | 0.042941765 |
| 13 | 0.087854302 |
| 14 | 0.043029680 |
| 15 | 0.057375649 |
| 16 | 0.046860167 |
| 17 | 0.047208917 |
| 18 | 0.073825322 |
| 19 | 0.067943363 |
| 20 | 0.060926533 |
| 21 | 0.040433937 |
| 22 | 0.072121245 |
| 23 | 0.065616180 |
| 24 | 0.069885489 |

Mean `0.057310971`, standard deviation `0.012430040`, 95% CI `±0.004973052`.

## Annealing ablation

Same seed (42), same configuration, with the two annealing switches changed:

| Learning-rate anneal | Entropy anneal | Exploitability |
|---:|---:|---:|
| 0 | 0 | 0.045099369 |
| 0 | 1 | 0.106899342 |
| 1 | 0 | 0.139822951 |
| 1 | 1 | 0.146774005 |

The selected fixed coefficients are better for this implementation than the
default annealing behavior.
