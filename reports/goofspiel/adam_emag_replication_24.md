# Adam + EMAg 24-seed replication

All runs use the same selected PufferNet 64x2 configuration, fresh seeds 1001--1024,
four-card imperfect-information Goofspiel, and no exact solver during training. Exact
exploitability was measured at two fixed training budgets.

| Checkpoint | Mean | Sample SD | 95% CI |
|---|---:|---:|---:|
| Live, 16.384M | 0.138738 | 0.030587 | [0.125822, 0.151654] |
| EMA, 16.384M | 0.039016 | 0.003760 | [0.037428, 0.040603] |
| Live, 29.4912M | 0.082501 | 0.019209 | [0.074390, 0.090613] |
| EMA, 29.4912M | 0.037452 | 0.014045 | [0.031521, 0.043383] |

The paired EMA improvement from 16.384M to 29.4912M was 0.001564, with a 95%
confidence interval of [-0.004621, 0.007748]. The longer budget therefore did not
produce a statistically reliable improvement in this replication; it primarily increased
between-seed variance.

At 29.4912M, 0/24 seeds reached exploitability <= 0.012, 3/24 reached <= 0.02,
and 21/24 reached <= 0.05. The range was [0.014108, 0.066356]. The earlier
single-run result of 0.011970 was not reproduced by any unseen seed and should be treated
as a lucky seed/time result, not typical performance.

The full per-seed measurements are in `adam_emag_replication_24.tsv`.

## Matched no-EMAg ablation

The same 24 seeds were rerun to 16.384M steps with only
`train.emag_kl_coef=0`. This disables both magnet regularization and EMA
checkpoint generation. Exact exploitability was 0.962726 mean, 0.029118 sample
standard deviation, and [0.950430, 0.975021] 95% CI. The range was
[0.902654, 0.998565].

At the same budget, the live policy trained with EMAg had mean 0.138738 and its
EMA policy had mean 0.039016. The current hyperparameters are therefore strongly
dependent on EMAg. This matched ablation does not establish that ordinary PPO
cannot work: these hyperparameters were selected while EMAg was active and are
not the older plain-PPO configuration.

The no-EMAg per-seed measurements are in `adam_noemag_replication_24.tsv`.
