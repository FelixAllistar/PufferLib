# Realized-descriptor population prototype

This is an offline, environment-independent archive selector. It does not
modify training, load banks, apply personality rewards, or change a live league.
It is a first testable component, **not** an implemented PSRO/Protein search loop.

## Data contract

An evaluation bundle contains `version=1`, a `schema_id`, a `panel_id`, a
descriptor `schema`, and candidate `records`. Each record has:

- `id`: unique immutable candidate label; include old league members and new
  candidates in the same selection, not just new checkpoints.
- `descriptors`: realized measurements, not training reward coefficients.
- `payoffs`: common opponent/scenario IDs mapped to `{score, games}`. Score is
  mean bounded [0,1] quality; Pokémon uses win + half draw. Noncompetitive
  adapters can use scenario quality with a documented fixed normalization.
- Optional provenance and raw episode data, retained by the evaluator.

Schema fields are either `scalar` (fixed normalized [0,1]) or `distribution`
(`size` plus nonnegative counts/mass with positive total), with optional
nonnegative `weight`. Categorical labels/order and scalar normalization must
remain fixed for a schema version. Missing data is an error, not a measured zero.
The panel ID must cover opponents/scenarios, rules, seeds and evaluator version.

Scalar distance is absolute normalized difference. Distribution distance is
square-root Jensen-Shannon divergence with base-2 logs. Groups combine through
weighted RMS. Histogram counts are normalized independently; a run lasting
twice as long is not novel solely because it has twice as many observations.

## Selection

```
python3 -m pufferlib.population evaluations.json --output selection.json
python3 -m pufferlib.population incumbents.json candidates.json --capacity 16
```

Multiple bundles must have identical schemas and panel IDs and disjoint labels.
Existing output files are never overwritten. This outputs a recommendation;
it does not copy/delete policies or create a trainer opponent manifest.

1. Seed with highest uniform-panel average quality.
2. Admit candidates with a confirmed matchup improvement over **all already
   selected members**, or sufficient descriptor novelty subject to a quality gap.
3. Repeat until capacity or no eligible candidates. Report each admission reason.

Matchup improvement compares a candidate's lower Hoeffding bound to the best
selected upper bound plus a margin. Bounds use a union bound over the provided
candidate/panel pairs at 95% confidence and assume independent bounded games.
This is deliberately conservative: small evaluation budgets rarely establish
coverage. Do not count correlated duplicate episodes as independent samples.
Counter-specialists can bypass the average-quality gate through this route.

`--no-competitive` disables the matchup route. `--quality-gap`, `--novelty`,
and `--capacity` configure the remaining selection. Candidate order does not
affect selection. The archive is grid-free and capacity-bounded.

## Limitations and next layers

- Descriptor novelty currently uses empirical means/histograms, **not**
  confidence intervals. Repeat evaluations on new seeds before accepting a
  strategic interpretation; especially beware sparse high-dimensional histograms.
- Quality and descriptor averages weight panel members equally. Panels must be
  representative, frozen for comparisons, and refreshed/re-evaluated explicitly.
- Averages can hide timing, correlation and opponent-conditional behavior.
  Adapters should add phase/conditional descriptors when their measurements are
  reliable; raw episodes allow later analysis without claiming those features exist.
- Descriptor choice/weights are hypotheses, not validated strategy discovery.
  Outcomes such as duration or HP may reflect opponent weakness rather than style.
- The greedy selector is not a Nash/meta-game solver. Restricted-panel coverage
  does not establish unexploitable play; it can miss useful combinations of policies.
- At capacity, useful policies can be excluded. `capacity_reached` signals this;
  there is no claim all matchup coverage was preserved.
- No automatic live-league migration. Kaggriculture's existing JSD/PSRO remains
  unchanged. A farming adapter must measure realized production/investment/time
  profiles before this selector can characterize farming strategies.

Future search integration: Protein proposes bounded training/reward recipes;
trainers produce candidate checkpoints; adapters evaluate realized descriptors
and true quality; the archive selects; an optional PSRO module solves mixtures.
Recipe bounds must not be inferred from descriptor coordinates as if reward
weights mapped directly to behavior. Keep exploratory shaping separate from
the unchanged evaluation objective.
