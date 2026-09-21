# Manual event-reward experiment

This is a fixed steering test, not CMA/QD or explicit team search. Each of three
matched training seeds runs five conditions from the same competent win-only
self-play checkpoint: control, sleep +0.5/-0.5, paralysis +0.5/-0.5. Each gets
100M requested steps. Training uses learner/history self-play only, with fresh
optimizer state and identical hyperparameters. No species-specific rewards.

Sleep means successfully sleeping an opponent in turns 1–5 (once per battle,
excluding Rest). Paralysis counts distinct opposing party members, capped at
three and divided by three. Failed moves and switch-in status announcements do
not count. Each step pays `w · (own capped progress − opponent capped progress)`
in addition to win/loss. Rewards are zero-sum. There is no terminal cancellation;
each condition's event contribution is bounded by ±0.5 over a battle. A negative
weight discourages one's own event *and rewards the opponent causing it*; it is
not a pure unilateral aversion. HP/KO and old potential-based objectives are off.

Every condition is evaluated against the same four snapshotted self-generated
policies, at 64 games each. Evaluation uses actual battle outcomes, not shaped
return. `summary.json` reports win score, own/opponent event means, per-seed
values and matched control differences. Raw paralysis means are uncapped;
individual `result.json` files also contain normalized capped event means and
team/lead descriptors. Draws count as 0.5 win score. This small test is exploratory;
do not infer a reliable effect from a single seed or choose a winner by return.

Build separate binaries (leaves the usual `puffer` and `pokemon` untouched):

```sh
OUTPUT_NAME=build/pokemon/pokemon_behavior NATIVE_OUTPUT_NAME=build/pokemon/puffer_behavior bash build.sh pokemon
python -m unittest discover -s ocean/pokemon/tests
make -C ocean/pokemon behavior-test
PK_OPT=ReleaseSafe bash ocean/pokemon/build_engine.sh test
bash ocean/pokemon/run_behavior_local.sh
```

The launch script contains the local parent/panel paths. For another experiment,
invoke `behavior_experiment.py --help` and use a fresh output directory. The
runner fingerprints inputs, snapshots parent/panel weights, validates the trainer
handshake and saved config, and resumes completed jobs. Failed training logs are
preserved and do not silently trigger retraining. Results are not admitted into
the old QD archive; its checkpoints and binaries remain intact.
