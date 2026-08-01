# Four-card Goofspiel Muon + EMAg replication

Twenty-four independent seeds used the native 64x2 PufferNet/MinGRU policy,
Muon, corrected EMAg, asynchronous collection, and CUDA graphs. Exact
exploitability was computed from every saved EMA checkpoint.

## Reproducible fixed stopping point

The lowest population mean occurs at the same fixed 24.576M-step checkpoint for
every run:

| Checkpoint | Mean | SD | 95% CI |
|---:|---:|---:|---:|
| 24.576M EMA | **0.017738** | 0.004552 | 0.015816--0.019660 |
| 32.768M EMA | 0.046134 | 0.012519 | 0.040848--0.051421 |
| 32.768M live | 0.099469 | 0.018534 | 0.091642--0.107295 |

The confidence intervals are two-sided 95% Student-t intervals with 23 degrees
of freedom. The fixed checkpoint is the valid reproducible comparison. It does
not choose a different stopping point for each seed.

## Exact population learning curve

| Steps | Mean | SD | Minimum | Maximum |
|---:|---:|---:|---:|---:|
| 1.638M | 0.734784 | 0.006106 | 0.725348 | 0.746522 |
| 3.277M | 0.664967 | 0.006518 | 0.651444 | 0.679848 |
| 4.915M | 0.536250 | 0.010854 | 0.515848 | 0.559416 |
| 6.554M | 0.374729 | 0.011210 | 0.354227 | 0.401086 |
| 8.192M | 0.243393 | 0.008116 | 0.224336 | 0.260092 |
| 9.830M | 0.155670 | 0.006204 | 0.138145 | 0.166029 |
| 11.469M | 0.098998 | 0.004720 | 0.086691 | 0.106057 |
| 13.107M | 0.065993 | 0.003031 | 0.060720 | 0.072310 |
| 14.746M | 0.046472 | 0.002306 | 0.042672 | 0.052599 |
| 16.384M | 0.034654 | 0.001992 | 0.030774 | 0.039385 |
| 18.022M | 0.027588 | 0.002026 | 0.023328 | 0.031483 |
| 19.661M | 0.022414 | 0.001987 | 0.018747 | 0.026822 |
| 21.299M | 0.019370 | 0.002154 | 0.015433 | 0.024541 |
| 22.938M | 0.018319 | 0.003847 | 0.013404 | 0.029468 |
| 24.576M | **0.017738** | 0.004552 | 0.013566 | 0.035207 |
| 26.214M | 0.018765 | 0.005037 | 0.012626 | 0.029622 |
| 27.853M | 0.020973 | 0.008407 | 0.010538 | 0.042915 |
| 29.491M | 0.026090 | 0.009326 | 0.015248 | 0.050176 |
| 31.130M | 0.033617 | 0.010595 | 0.019218 | 0.063512 |
| 32.768M | 0.046134 | 0.012519 | 0.024123 | 0.078692 |

All seeds initially follow the same learning curve. After approximately 24M
steps the mean degrades and between-seed variance grows, so longer training is
not a substitute for checkpoint selection.

## Post-hoc per-seed oracle

Selecting each seed's best checkpoint after exact evaluation gives
0.014914 +/- 0.000912 (95% CI), with SD 0.002161. Best checkpoints ranged from
21.299M to 29.491M steps and averaged 24.917M. This is useful for diagnosing the
optimizer trajectory, but it is an oracle result and must not be presented as a
fixed training protocol.

The full per-seed results are in
`reports/goofspiel/muon_emag_best_replication_24.tsv`.
