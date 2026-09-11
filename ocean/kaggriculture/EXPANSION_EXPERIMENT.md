# Frozen early-expansion experiments

One editable config remains `config/kaggriculture.ini`. Preparation captures its
resolved settings in an immutable JSON plan; subsequent config edits cannot
alter the captured overrides. This is an experiment record, not another config
to maintain. No automatic submission, promotion, pruning, or config rewriting.

```bash
cd /workspace/PufferLib
/venv/main/bin/python ocean/kaggriculture/expansion_experiment.py prepare \
  --output logs/kaggriculture/early_expansion_v1_20260908 \
  --league saved/kaggriculture_league_obs1_expansion_v1/league.ini
/venv/main/bin/python -u ocean/kaggriculture/expansion_experiment.py run \
  --output logs/kaggriculture/early_expansion_v1_20260908
```

The six trials are deadline {240,300,360} × expansion scale {3,10}, fresh seed42
starts, 300M steps each. Terminal cash=4 and terminal win=1; other independent
shaping branches and state resets are off. Expansion targets and optimizer
settings come from the captured config. LR/entropy annealing settings are also
captured, not silently changed. An absent external magnet stays absent.

Opponents are copied, content-hashed and observation-version tagged. The native
training sampler uses these fixed manifest weights (PFSP alpha=0, uniform
mixture=0), without rolling snapshots. Native bot proportions remain captured.
The GPU evaluation panel is equally weighted across those opponents, with fixed
seeds and both seats. It is deliberately independent of shaped training return.

Each run retains approximately 100M/200M/final checkpoints, evaluated in both
argmax and stochastic modes after training finishes. Training time at each save
excludes subsequent evaluation. Actual saved steps are reported; rounding does
not pretend every checkpoint is exactly on the requested boundary.

Outputs: `summary.tsv`, `report.md`, `learning_curves.json`,
`curves_steps.svg`, `curves_training_seconds.svg`, per-checkpoint matches,
behavior episodes/phase traces, starting configs, checkpoint hashes, and logs.
The reports appear after the first evaluated checkpoint, not at queue startup.

Behavior diagnostics use the native CPU simulator/executor and NumPy policy
inference, not GPU arithmetic. They are separately labelled and are not pooled
into match scores. First milk means ready OR harvested milk; milk-by300 is
harvested units. Never-cow/never-milk rates accompany conditional first-event
means. Productive extra tile-turns count occupied crop/animal tiles outside the
initial plot, not a guarantee of profitable production. One seed × both seats
per opponent is a cheap diagnostic sample, not a robustness claim.

Run the same `run` command to resume. Completed evaluations are skipped.
Interrupted training restarts fresh under a new attempt ID; it does not silently
load a partial checkpoint without its optimizer state. Failed/OOM trials stay
recorded and are skipped. Evaluation failures stop the queue for repair and can
resume without retraining. A lock rejects duplicate queue invocations. Changes
to frozen binaries/scripts/models invalidate resume rather than mix versions.
`--max-trials 1` limits an invocation to one completed trial (useful for smoke tests).

Keep winners on both quality and training-time axes. Repeat promising settings
with additional training seeds and a held-out opponent/seed panel before PSRO
promotion; this six-run screening is not statistical proof of superiority.

## Identity and league repair

`eval_population.sh` collapses identical checkpoint content with the same
observation layout before constructing the payoff matrix, recording aliases.
`psro.sh` aggregates historical alias weights, reads startup architecture metadata,
refuses accidental reuse of an output prefix, and avoids overwriting different
models whose reused training IDs produce the same admission filename.

`repair_league_weights.py` recalculates weights for existing members from an
evaluated matrix/meta-strategy, with the existing 70/25/5 role shares. It defaults
to dry-run; `--apply` first archives the league and changes only weights/roles.
The September8 repair used a new 7-policy, 100-games-per-pair matrix at
`logs/kaggriculture/league_identity_repair_20260908`, not the duplicate-containing
old matrix. Members, learner marker and editable config were retained.
