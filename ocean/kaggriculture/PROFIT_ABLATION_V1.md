# Profit ablation and reference replay

Reference: `/mnt/c/Users/sunde/Downloads/106946670.json`, Kaggriculture 1.32.7,
episode106946670, seed1651054280. Players: Matthew Huang and SpaTaro.
Final public cash: $97,299 and $100,967. Source replay remains untouched.

Comparison below is **one external replay versus 14 CPU-native diagnostic
episodes** from the +199.23M continuation checkpoint. Seeds, opponents and
inference backend differ: descriptive evidence, not a causal benchmark.

| Turn | Matthew cows | SpaTaro cows | PPO mean cows | Matthew/SpaTaro plots | PPO mean plots |
|---|---:|---:|---:|---|---:|
| 24 | 2 | 2 | 3.4 | 1 / 1 | 1 |
| 48 | 2 | 2 | 4.7 | 1 / 1 | 1 |
| 96 | 4 | 4 | 5.9 | 1 / 1 | 1 |
| 144 | 4 | 5 | 6.7 | 1 / 1 | 1 |
| 192 | 8 | 7 | 7.4 | 2 / 2 | 1 |
| 240 | 9 | 10 | 7.6 | 2 / 2 | 3 |
| 288 | 10 | 10 | 7.6 | 3 / 3 | 3 |
| 360 | 10 | 10 | 7.5 | 3 / 3 | 3 |
| 480 | 10 | 10 | 6.6 | 3 / 3 | 3 |
| 600 | 10 | 10 | 6.4 | 3 / 3 | 3 |

At turn24 the reference players have 2 cows + 2 sheep each; PPO has 9.6 animals
total and 9.6 plants on average. At turn288 the reference players have 17 and22
animals; PPO has11.1 animals. PPO is not simply late to its first animals: its
early allocation and later herd development differ. Reference farms also switch
from substantial early melon plantings toward strawberries/wheat; the existing
PPO diagnostic does not separate crop types, so that comparison is incomplete.
Cow counts falling later do not by themselves establish neglect as the cause.

## Matched experiment

Remote output: `/workspace/PufferLib/logs/kaggriculture/profit_ablation_v1`.
Four **continuations**, each300M additional steps, all loading the same frozen
policy and EMAG reference from the +199.23M checkpoint. Optimizer state restarts
equally for all arms. This is not four fresh random initializations.

| Arm | Expansion scale | Terminal win scale |
|---|---:|---:|
| 0 control | 3 | 1 |
| 1 | 0 | 1 |
| 2 | 3 | 0 |
| 3 | 0 | 0 |

Terminal cash stays4. Deadline240, targets3 total plots /42 plants /15 animals,
LR0.0004 (no annealing), EMAG0.01/tau0.1, 4096 agents, horizon256,
minibatch2048, architecture256x3, obs1/macro2. Other independent shaping and
reset-state sampling remain off. Same frozen seven opponents and weights.
No active config rewrite, automatic promotion, or submission.

Parent baseline and ~100M/~200M/final checkpoints are evaluated against the same
panel in deterministic and stochastic modes. `cash_ranking.tsv` ranks each mode
separately by money; `herd_timeline.tsv` reports cow/animal/plant/plot/cash means
at fixed turns. `summary.tsv` and `report.md` retain generic experiment output;
the cash ranking also includes the win coefficient that generic headings omit.

Resume command, from the remote repo:

```bash
/venv/main/bin/python -u ocean/kaggriculture/profit_ablation.py run \
  --output logs/kaggriculture/profit_ablation_v1
```

Completed trials/evaluations are retained. Interrupted training restarts from
the same parent under a new attempt ID, not an incomplete optimizer resume.
OOM/failures are recorded and skipped. A file lock rejects duplicate runners.

This does not rule out an encoder bottleneck. If reducing the shaping leaves
the cash plateau intact, do a matched representation experiment rather than
asserting that yet another reward coefficient must fix it. The $100k target
remains an evaluated panel mean, not one replay, a dashboard peak, or a promise.
