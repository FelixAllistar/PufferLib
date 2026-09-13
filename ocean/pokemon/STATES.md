# Fixed state-bank pilot

This is DAGS-style auxiliary training with deliberately stratified state coverage,
not a literal reproduction of OmniReset's robotics generators or a new PPO solver.

## Contract

- Half of episode starts are ordinary private drafts. Half restore a state, with
  opening/midgame/endgame probabilities 0.4/0.4/0.2 conditional on restoration.
- Every transition uses the normal rules, observations, legal masks and terminal
  win/loss reward. The original 512-update draw clock is preserved after restore.
- Observation byte 476 is a public persistent source flag, raw uint8 0/1 for both
  players. Bytes 477–479 remain reserved. Observation width and action ABI remain
  640/160. Saved configs identify reset observation version 1.
- Root evaluation always disables bank starts. The flag remains zero for the
  entire evaluation game. Dataset-start scores are never selection metrics.
- Both players start bank episodes with zero recurrent memory through the existing
  terminal-state reset path. This is an auxiliary memory-truncated task, not a
  reconstruction of the collector policy's private recurrent history. Public
  move/appearance history is part of the restored engine wrapper.
- The migration copies original checkpoints and zeroes only the formerly reserved
  flag encoder column. Original weights/configs are untouched. This preserves root
  outputs and makes frozen archived policies initially ignore the auxiliary flag.
  Do not apply this migration to a policy that has already learned the flag.
- Bank choice uses a separate per-environment stream. Restoration preserves the
  ordinary reset stream and resamples only future engine randomness, not existing
  hidden sleep/confusion durations. Both explicit and terminal resets use the same
  episode-start helper; terminal rewards remain attached to the completed game.

## Collection

The collector plays 8,192 new games using six archived, self-generated policies:
the five manual seed-101 conditions plus the generation-1 specialist. Each policy
is paired with all others in a deterministic schedule. One eighth of games uses
exploratory legal drafts and occasional uniformly legal battle actions. These
generate states; their actions and eventual outcomes are not training targets.

Per game, retain at most one opening, four midgame and three endgame positions,
sampled by within-phase reservoir sampling. A position with at most three live
Pokémon on either side is classified as an endgame. Reject terminal/invalid
states. Deduplicate exact snapshots after canonicalizing future RNG fields.
Cap coarse team-hash/material/status strata, with bank capacities 8,192/32,768/
24,576. These are upper bounds, not requested counts: do not pad with duplicates.
The coarse strata are not guarantees of comprehensive strategic coverage.

Snapshots are trusted local binary assets, validated once before workers start.
The header checks format version, record size, catalog hash, pinned engine/schema
identity and payload checksum. Experiment manifests additionally SHA-256 all
inputs. The native hot path only samples indices, copies state, reseeds future
chance draws and regenerates observations/masks. No file I/O or Python per reset.
The on-disk struct is build-specific; bump the schema when its meaning changes.

## Matched experiment

Parent: the manual seed-101 win-only control (already trained with a win-only
critic). Two matched seeds, 6101 and 6102; root-only versus reset probability 0.5.
All six collectors form the same uniformly sampled frozen training pool in both
arms. The two resident opponent banks rotate every 250,000 agent steps. This
holds the opponent distribution fixed instead of introducing population-restart
differences between arms. It is a best-response/generalization pilot against a
fixed self-generated population, not a claim of equilibrium convergence.

Each job requests 20M total agent steps, including frozen rows, rounded down to
complete rollouts. LR 1e-4, entropy 5e-4, gamma .999, GAE .995, horizon 512,
1,024 total agents, fresh optimizer, no annealing or EMAg. Checkpoints approximately
every 5.24M steps. Counterbalance arm execution order across training seeds.
Four separate archived opponents are evaluation-only, with 64 games each under
two fresh seeds (17,101 and 17,102): 512 seat-balanced root games/checkpoint.
These opponents were used as holdouts in the earlier event experiment; they are
excluded from this pilot's bank generation and training population.

Training telemetry reports root/reset scores separately, actual episode and step
fractions, and bank mix. Preserve all checkpoints, including regressions. Compare
root evaluation score versus steps and training seconds; count bank construction
as an additional one-time treatment cost. Two seeds are exploratory evidence.

## Build and run

```sh
source /home/felix/puffertank/.venv/bin/activate
NATIVE_OUTPUT_NAME=build/pokemon/puffer_states OUTPUT_NAME=build/pokemon/pokemon_states bash build.sh pokemon
make -C ocean/pokemon states state-test state-benchmark
python ocean/pokemon/tests/test_state_experiment.py
python ocean/pokemon/state_experiment.py prepare --out runs/pokemon_states_20260913
bash ocean/pokemon/run_states_local.sh
```

The runner refuses changed inputs or silent reruns of failed attempts. It writes
bank/migration manifests, collection logs, benchmark logs, per-job commands,
checkpoints, evaluations, `report.json` and `REPORT.md`. `complete.json` means all
four jobs and their evaluations finished. Existing ordinary binaries and prior
experiments are not overwritten by this build.
