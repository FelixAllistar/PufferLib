# Goofspiel baseline comparison

Both runs used 13 cards, random prize order, perfect information, and 500M
agent steps. `history_seed73` used one 25% frozen historical bank;
`live_seed73` used only current-policy mirror self-play.

The historical final policy beat the live final policy with a two-sided score
of approximately 0.804 over 65,536 games per orientation.

The live lineage contains a clear checkpoint cycle:

- 400M beats 300M: 0.641
- 300M beats 500M: 0.648
- 500M beats 400M: 0.749

The historical lineage also contains a sharper cycle:

- 301.5M beats 294.9M: 0.906
- 500M beats 301.5M: 0.774
- 294.9M beats 500M: 0.743

The CSV matrices average both seat orientations. The JSON files retain each
orientation separately.

## Five-card exact baseline

`goofspiel5_history_seed1` used a 32x1 MinGRU, one 25% historical bank, and
100M agent steps. Exact exploitability increased during training:

| Checkpoint | Exploitability |
|---:|---:|
| initialization | 0.805443 |
| 49.8M | 0.871559 |
| 99.9M | 0.964826 |

The final policy nevertheless beat the midpoint policy with a two-sided score
of approximately 0.641 over 65,536 games per orientation. It also scored about
0.639 against the 13-card `goofspiel1` policy when both played five-card rules.
This separates lineage-relative improvement from equilibrium robustness: the
final policy became stronger against known policies while becoming more
exploitable by an exact best response.

A six-checkpoint five-card cross-play audit is in `fivecard_lineage.*`. Unlike
the 13-card runs, this lineage is mostly a strength ladder: 60M beats 40M with
score 0.693, while the 60M, 81M, and 100M policies are close and draw often.
Consequently, one-lineage PFSP has little strategic diversity to select here.
Independent seeds or trained responses are required before treating this as a
population league.

## Five-card population probe

Two additional 100M-step historical-selfplay seeds produced different mirror
draw rates, but their finals were strategically close in two-sided cross-play:

| Pair | First-policy score | Draw |
|---|---:|---:|
| seed 1 vs seed 2 | 0.518 | 0.643 |
| seed 1 vs seed 3 | 0.487 | 0.644 |
| seed 2 vs seed 3 | 0.485 | 0.665 |

Their exact exploitabilities were 0.964826, 0.966797, and 0.949610. Different
mirror behavior therefore did not imply useful strategic diversity.

`goofspiel5_response_seed3_v1` trained a fresh policy for 50M steps solely
against frozen seed 3. It improved from score 0.159 to 0.473 but had not become
a response. `goofspiel5_response_seed3_v2` continued it for 100M steps and
produced a nearly deterministic specialist:

| Opponent | Response score | Draw |
|---|---:|---:|
| seed 1 | 0.829 | 0.013 |
| seed 2 | 0.768 | 0.021 |
| seed 3 | 0.922 | 0.026 |

Both seat orientations agree. Its exact exploitability is 0.999999903, nearly
the maximum. It is a useful new population adversary but a worse standalone
policy. This reproduces the important Bomberman result in a game with exact
ground truth: response training creates strategic diversity, while relative
win rate alone can select extremely exploitable policies.

The next population step is a response to this specialist. Once responses form
a cycle, solve a restricted zero-sum meta-strategy from their full payoff
matrix and train against that explicit weighted mixture. Do not use Elo as the
primary population state: a scalar rating discards the cycles under study.

## Overtraining and capacity audit

`exploitability_curves.tsv` records exact exploitability every approximately
10M steps for all three historical seeds and the cumulative 150M response
lineage. The ordinary policies consistently improve around 30-40M and then
collapse:

| Policy | Best checkpoint | Best exploitability | Final exploitability |
|---|---:|---:|---:|
| seed 1 | 40.6M | 0.747022 | 0.964826 |
| seed 2 | 30.1M | 0.766545 | 0.966797 |
| seed 3 | 30.1M | 0.740699 | 0.949610 |

The response becomes more exploitable monotonically, reaching approximately
1.0 by 80M cumulative steps. These runs were overtraining against incomplete
opponent distributions.

`capacity.tsv` compares identical-seed historical self-play. At 40M, 32x1 is
better than both 64x2 and 128x2 under unchanged hyperparameters:

| Model | Parameters | 40M exploitability |
|---|---:|---:|
| 32x1 | 11.0K | 0.747022 |
| 64x2 | 40.4K | 0.852205 |
| 128x2 | 130.0K | 0.802488 |

Capacity is therefore not the present bottleneck. Larger models may require
separate optimization tuning, but increasing size alone is harmful here.

## Short population updates

The first three-policy empirical game formed a cycle: general `S` beats counter
`C` with score 0.670, `C` beats specialist `R` with 0.866, and `R` beats `S`
with 0.573. Its restricted equilibrium was approximately `(S=.601, R=.280,
C=.119)`.

Training against that mixture with the original entropy coefficient and
learning rate produced a policy that beat every population member but worsened
to 0.918758 exact exploitability. Ten-million-step ablations identified both
policy collapse and update size as important:

| Entropy | Learning rate | Exploitability |
|---:|---:|---:|
| 0.01 | 0.0010 | 0.899137 |
| 0.02 | 0.0010 | 0.799540 |
| 0.05 | 0.0010 | 0.784673 |
| 0.10 | 0.0010 | 0.777493 |
| 0.20 | 0.0010 | 0.782721 |
| 0.10 | 0.0003 | 0.741368 |
| 0.10 | 0.0001 | 0.742809 |

After recomputing the restricted mixture, a second 10M update with entropy
0.10 and learning rate 0.0001 reached the current best exact exploitability,
0.737904. It scores approximately 0.515 against `S`, 0.502 against `R`, 0.692
against `C`, and 0.499 against the previous population policy. This is the
first single policy to improve exact robustness while remaining balanced
across the discovered strategic population.

Current best checkpoint:

```text
checkpoints/goofspiel/goofspiel5_meta_psro2_lr1e4/0000000009961472.bin
```
