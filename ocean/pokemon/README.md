# Pokémon conversion to PufferLib 5.0

The native battle engine and semantic free-pick game core are preserved from
`5c` at `036cf4251`. This is a battle simulator, not a ROM emulator; no ROM is
needed. The engine, drafting, legal masks and observations are unchanged.
The current source defines ABI 3, 168 actions and **648 observation bytes**.

```sh
make -C ocean/pokemon test
make -C ocean/pokemon sanitize
make -C ocean/pokemon semantic-test
make -C ocean/pokemon viewer adapter-test
make -C ocean/pokemon benchmark
```

The build script fetches `pkmn/engine` at
`9b88fd6c5467f703c38951d5b2e8a660314d410b` and checksum-verifies Zig 0.16.0 on
Linux x86-64, under ignored `build/pokemon/`. Set `ZIG` to an existing 0.16.0
compiler to reuse it. The engine's license remains in its checkout; dataset
provenance and licensing are in [data/NOTICE.md](data/NOTICE.md).

Qualification so far: bridge tests cover private-state non-leakage, unchanged
battle transitions under event recording, and successful behavior-event
accounting. C tests run 128 seeded sampled-team/private-draft games, checking
legal actions, no engine errors, repeatable observations/masks/results and
distinct species. Sanitizer builds instrument the C wrapper; Zig uses
ReleaseSafe checks rather than C sanitizers.

The semantic CPU model and mechanics tables are also preserved unchanged.
`semantic-test` checks serialized weights, batched versus independent inference,
and finite recurrent outputs through all draft/battle phases at H16/L1 and
H32/L2. To compare against a preserved old checkout, run:

```sh
make -C ocean/pokemon semantic-legacy-test LEGACY_CPU=/absolute/old/src/puffercpu.h
```

That optional test compiles the old CPU implementation separately and compares
exact output-trace digests with identical weights, inputs and resets. Local
scalar builds match; this does not establish CPU/GPU numerical parity.

The standalone semantic evaluator and adapter tests are now available:

```sh
./build/pokemon/viewer eval random random --games=32 --profile
./build/pokemon/viewer eval PATH_A.bin PATH_B.bin --games=256
./build/pokemon/viewer matrix PATH_A.bin PATH_B.bin PATH_C.bin --games=256
./build/pokemon/viewer watch PATH_A.bin random
```

Run from the repository root. Checkpoints need a sibling `config.ini` or the
matching run config under `logs/pokemon/`, with semantic policy/ABI version 3
and matching rules hash. The evaluator supports deterministic/stochastic
actions, team/lead constraints, alternating seats and profile output. `--emag`
can evaluate preserved legacy `.emag` weights; it does not add EMAg training.
Window controls and visual output still need interactive qualification.

Offline state-bank collection and auditing are available independently of the
trainer:

```sh
make -C ocean/pokemon state-tools state-test
./build/pokemon/collect_states NEW_BANK.bin 128 871 random random
./build/pokemon/audit_states NEW_BANK.bin 871 128 2
```

Replace the two `random` entries with compatible checkpoint paths for archive
collection. The audit takes the collection seed, game count and policy count;
it verifies provenance, per-game sampling limits and phase partitions. The
collector refuses to overwrite an existing bank. Tests cover file corruption,
deterministic restoration, masks, reset flags, rewards and episode clocks,
plus repeatable collection and invalid-provenance rejection. Generated banks
are external binary assets, not Git source. This does not yet enable reset
scheduling in the 5.0 trainer.

The pinned data generators can also be checked without rewriting their outputs:

```sh
make -C ocean/pokemon validate
make -C ocean/pokemon core-test
```

`validate` needs Node/npm and installs Pokémon Showdown 0.11.11 under ignored
`build/pokemon/validation/`, with install scripts disabled. It validates the
556 sourced set variants and checks generated free-pick tables against the
saved exhaustive legality audit. Audit reuse verifies the validator-source
fingerprint, source hashes and move pools; it does not rerun all 2,931,585 set
combinations. To rerun those, use `node ocean/pokemon/generate_freepick.cjs
--check` without `--reuse-audit`. Do not use `import_sets.cjs --refresh` unless
deliberately replacing the pinned source dataset.

`core-test` requires OpenMP (for Clang, install its OpenMP development library).
It checks all 540,274 species triples, private core masks, expert-team binding
and equal trajectories with one versus four workers. It invokes preserved
environment callbacks directly with metadata-only expert fixtures, not a 5.0
training run. Removed legacy config keys exist only in the test fixture;
native reset/league scheduling remains unported.

The semantic CUDA encoder/decoder source is now ported to 5.0 tensor types.
`make -C ocean/pokemon encoder-test` builds and runs an isolated FP32 component
test against the preserved CPU model (three rows, four synthetic observation
batches). It checks encoder outputs and all 168 action logits plus value,
matching parameter/gradient layout, finite gradients, and 172 centered numerical
derivatives across all twelve parameter tensors and the decoder's hidden-state
input. Tests include draft, move-selection and battle-specific candidate paths.
This does not yet qualify recurrent training, real-game GPU observations,
CUDA graphs or BF16.

The decoder needs the current observation to identify candidate moves/species.
The approved optional `Decoder.bind_observation` callback now supplies it in
both rollout and training forward paths. The shared change is seven lines in
`src/algo.cu`; semantic lookup and all model math stay here. Tests exercise both
real architecture entry points across four batches and all 172 derivatives;
recurrence is replaced with fixed hidden inputs to isolate decoder behavior.
This does not qualify end-to-end Pokémon training. Default decoders leave the
callback null; a Goofspiel four-bank GPU graph training smoke still passes.

Native training now builds with `./build.sh pokemon puffer_pokemon --float`;
run `./puffer_pokemon train`. The normal build also builds/links the pinned
engine. `./build.sh pokemon pokemon_viewer --cpu` selects the semantic CPU
evaluator, not the generic MLP viewer. Optional environment configuration and
post-step hooks initialize the curriculum and complete resets after workers
finish, before observations are uploaded. Core coverage requires one buffer,
synchronous rollout, and recurrent carry; the config now reflects this and
removes retired EMAg/PFSP/priority-replay keys.

Local SM61 FP32 H16/L1 training completes 2,048 steps with graphs off/on,
16 player rows, two CPU workers, a four-species core pool and legality auditing
enabled. Drafts complete, rewards/losses are finite and a checkpoint is saved.
This does not qualify default-scale performance or learning quality. Native
full league/experiment orchestration workflows
remain unfinished. Do not substitute the generic MLP for the semantic model
or load old checkpoints through a different architecture. Full legacy source,
generator scripts and experiment documentation remain in
`archive/5c-before-unification-20260924` while these ports proceed.

Native reset-bank qualification uses 1,024 audited snapshots from 128 seeded
random-policy games. A 2,048-step H16/L1 run with reset probability one, graphs
enabled and legality auditing passes, including all three reset phases.
The resulting checkpoint reloads for eight native evaluation games. Evaluation
always disables snapshot resets and forced-core drafting, even when training
config points to an unavailable bank; the regression test checks this without
opening the bank. This is simulator/loading evidence, not policy strength or
qualification of an existing user-collected bank.

Native checkpoint saves now invoke Pokémon's metadata writer. Each `.bin`
gets a `.bin.ini` with that snapshot's `core_next_assigned` and
`core_next_drafted`; parent `config.ini` retains the latest config for semantic
CPU evaluation. Transfer weights together with their metadata. To continue
the curriculum, explicitly pass those two values as `env.core_start_assigned`
and `env.core_start_drafted` alongside `base.load_model_path`, using the same
core pool/seed and model dimensions. This is weight/curriculum continuation,
not restoration of optimizer state, in-flight games or recurrent memory.
Assigned-but-unfinished drafts are not replayed automatically.

A native run with four checkpoint saves preserves earlier cursors (12 assigned)
while the final snapshot records 18 assigned/12 drafted. The CPU semantic
evaluator loads the final checkpoint and completes four games. A subsequent
256-step native run starts from that explicit cursor and advances it. The
metadata regression also verifies later saves do not overwrite earlier sidecars.

### Preserved user models

The local `leagues/pokemon/free4/state.json` identifies 15 current ABI-3,
128×2 members. All 15 weight/config pairs match their recorded SHA256 values.
These files are ignored assets, not supplied by a clone. Preserve the state
file and each member's `current.path` together with its parent `config.ini`;
transfer only selected pairs when preparing a new host. Build a new fixed-roster
manifest using destination paths rather than copying the old league manager.
This preserves trained policies without restoring PFSP or QD orchestration.

The selected master is
`leagues/pokemon/free4/members/master/r000011-a001/weights.bin`:

- Weights SHA256: `6fdf3a19534b1ae8b614fd14d21e9344c4cfacf05e523921ca6f82a7d72c560b`.
- Paired config SHA256: `1ea81ac5ba98001bf873995010278b64fade91c8165d4e169bdc6ad370e093ac`.
- Rules SHA: `3b71ba8b20aad4803f0238df5eb9a4a9f5285a119b00cd953b45901e9f3481d6`.

That master completed a 2,048-step native 128×2 continuation, CUDA graphs on,
legal-action auditing and post-training evaluation. The smoke output is under
`build/conversion/checkpoints/pokemon/preserved_free4_master_20260925/`;
the original weights/config remain unchanged. This is compatibility evidence,
not a recommendation to replace the master with the short smoke checkpoint.

The older `named_roster/native_two_20260910.ini` instead references ABI-2,
160-action catalog policies. They remain preserved but cannot be relabeled as
ABI 3 or used with the current 168-action semantic network. Native learner
loads now invoke the existing ABI/policy/rules validator through the optional
load hook; byte-count compatibility alone is insufficient. The integration
tests reject ABI-2 metadata even when the underlying weight dimensions match.

### Fixed named opponents

Set `env.native_league` to an INI manifest and `env.expert_fraction` to the
fraction of games facing those opponents. The remaining games use the current
learner on both seats. This is a fixed roster, not PFSP: opponent weights never
refresh during the run. All opponents share one architecture, which may differ
from the learner. Example manifest (paths resolve from the launch directory):

```ini
[native]
banks = 1
hidden_size = 128
num_layers = 2
rules_sha = COPY_FROM_OPPONENT_CONFIG
opponents = saved/pokemon/opponents.txt
[bank.0]
path = saved/pokemon/expert/model.bin
team = species:65,128,143
lead = 65
```

`opponents.txt` contains exactly one checkpoint path per line in bank order,
matching each `bank.N.path`. Each checkpoint's parent `config.ini` must match
its declared architecture, rules, ABI, prescribed team and lead. The environment
validates this before loading and binds each team's constraints during vector
initialization; only learner seats receive curriculum cores. Setting
`expert_fraction=0` disables the roster. Use external evaluation to compare
members; the retired league manager is not implicitly restored.

Qualification: native 2,048-step H8/L1 and configured H64/L2 learners run against
two H16/L1 generated experts with distinct teams/leads, graphs off/on and legality auditing. Original
expert files remain unchanged and curriculum drafts complete. Core/assignment
tests also preserve identical one/four-worker trajectories. Reproduce with
`POKEMON_TRAIN_BINARY=build/pokemon/native_train uv run --no-project --with pytest python -m pytest -q ocean/pokemon/tests/test_native_training.py`.

### Sequential round-robin workflow

`league.sh` / `freeleague.mjs` now target the native 5.0 trainer. They keep
explicit per-member budgets, fixed opponents within each stint, sequential
publication, frozen anchors, retry/resume, balanced-seat evaluation and live
dashboard logging. This is not PFSP, QD or a custom optimizer. Later members
face versions published by earlier members; interrupted stints retry against
their saved opponent snapshot. Optimizer and in-flight episode state are not
resumed.

```bash
./build.sh pokemon build/pokemon/puffer_native --float
make -C ocean/pokemon viewer
./ocean/pokemon/league.sh create leagues/pokemon/native5 \
    --master /absolute/path/to/compatible/weights.bin --new-policies 2 \
    --steps 1048576 --defer-init --binary build/pokemon/puffer_native
./ocean/pokemon/league.sh train leagues/pokemon/native5 --live
./ocean/pokemon/league.sh eval leagues/pokemon/native5 --games 128
./ocean/pokemon/league.sh export leagues/pokemon/native5
```

Use a new directory. State version 4 rejects old version-3 league state without
modifying it; import compatible checkpoints as seeds or frozen `--anchor`
members instead. `--master None` initializes independent random policies.
`--teams smogon` or `--teams experts-three` restores the old species/lead
presets while still learning moves. Retired trainer options such as EMAg and
PFSP are rejected rather than silently passed through. The exporter writes
both `native.ini` and its ordered `.opponents` file for the current loader.

Tests cover interruption/retry, checkpoint immutability, exact budgets, live
PTY output, export order and old-state rejection. A real two-member H16/L1
run completes two rounds, native opponent loading and balanced-seat evaluation:

```bash
node --test ocean/pokemon/tests/test_freeleague.mjs
POKEMON_TRAIN_BINARY=build/pokemon/puffer_native \
    node --test ocean/pokemon/tests/test_freeleague.mjs
```

The second command requires an idle GPU and the freshly built semantic viewer.
It is a workflow smoke test, not evidence of playing-strength improvement.

## Single-team Oak-scored sweep

`profiles/starmie_oak.ini` tunes one fresh H64/L2 policy. The team is the
best-scoring composition in the October 14 Oak comparison, `mie_egg_don`.
This experiment fixes its species, order, lead and moves; it does **not** claim
that any particular sampled moveset was proven best. Catalog controls are:

| Pokémon | Fixed moves |
|---|---|
| Starmie (lead) | Recover, Thunder Wave, Blizzard, Psychic |
| Exeggutor | Sleep Powder, Psychic, Explosion, Stun Spore |
| Rhydon | Earthquake, Rock Slide, Body Slam, Substitute |
| Chansey | Sing, Seismic Toss, Soft-Boiled, Thunder Wave |
| Snorlax | Body Slam, Reflect, Rest, Self-Destruct |
| Tauros | Body Slam, Hyper Beam, Blizzard, Earthquake |

These are existing catalog sets. The species composition comes from the
[OU sample team](https://pokepast.es/6eba7940bd5e4c94); Exeggutor's Explosion
replaces that sample's Mega Drain. Forced draft decisions still establish the
usual recurrent history and count toward the same step accounting as training.

```bash
# Export once on the machine containing the old league. Refuses overwrite.
python3 ocean/pokemon/sweep.py prepare
# Copy saved/pokemon/starmie_sweep/ to the same relative path on the GPU host.
python3 ocean/pokemon/sweep.py verify
./build.sh pokemon build/pokemon/puffer_native --float
bash ocean/pokemon/benchmarks/build.sh
# Run this inside tmux. The launcher execs the native sweep; stdout stays live.
python3 ocean/pokemon/sweep.py run
```

All 14 OU checkpoints, including the unrestricted OU master, are hash-checked
and frozen. The separate older `free4` master is not part of this H64/L2 bank.
Half the concurrent battle environments use those opponents; the other half
use current-policy self-play. Completed-game counts can differ because games
have different lengths. Since
both seats learn in current-policy games, 75% of counted agent steps are
learner steps: 33,554,432 counted steps = **25,165,824 learner steps per trial**.
Only initial/final learner checkpoints are saved, not periodic checkpoints.
All trials use the same initial seed and fresh weights; opponents are unchanged.

The native Protein sweep runs 16 full trials, default settings first. No short
learning-based eliminations are used. The RTX 5060 Ti profile uses 16,384 agent
rows, two buffers and four CPU threads per buffer. The search varies horizon
(32/64), minibatch (8192/16384/32768), learning rate, entropy, gamma, lambda,
replay ratio, clipping, value-loss weight and gradient clipping. The step budget
is identical for every rollout shape. Each final checkpoint plays 128 games
against each of three pinned human teams at Oak search budget 4096, with both
argmax and sampled actions (768 games/trial, balanced player seats). The
argmax score `(wins + 0.5*draws)/games` is Protein's objective; sampled score is
reported alongside it. An evaluator failure fails the trial, never substitutes
its training win rate. Training rewards remain ordinary zero-sum win/loss RL.

Results live beside the native log in `logs/pokemon/<run>.ini.oak/summary.json`,
with individual evaluations and timing records below it. Oak sees full state;
our policy uses its normal private observations. The pinned native engine is
authoritative, not byte-identical to Oak's fork. Every decision checks the
bridge's legal choices. Battle seeds are recorded, but Oak's internal search
randomness prevents exact rerun reproducibility. These are development-panel
results, not a Showdown rating or proof of general strength. Recheck the winning
settings with fresh training/evaluation seeds before full-league deployment.

CPU configuration checks: `python3 ocean/pokemon/tests/test_sweep.py`.
Hardware smoke tests can use `sweep.py run --mode=train` and explicit native
overrides such as `--train.total_timesteps=2097152 --oak.enabled=0`; these are
throughput/compatibility checks, not hyperparameter-ranking trials. Actual
sweeps reject budgets below 25 million learner steps. Layout overrides are
locked into all trials rather than silently searched via generic defaults.

October 15 Vast qualification (RTX 5060 Ti, FP32): all eight GPU integration
cases passed, including 14-bank loading with CUDA graphs on/off. Scalar/BLAS
inference produced identical actions and native states over 32 games and 1908
transitions (maximum logit difference 5.01e-6). The first complete native trial
returned its Oak score to Protein and the next trial started normally.

| Fixed Starmie policy | Learner steps | Argmax vs Oak | Sampled vs Oak |
|---|---:|---:|---:|
| Same-seed initialization | 0 | 7/384 wins (1.8%) | 0/384 (0.0%) |
| Default-settings trial | 25,165,824 | 87/384 (22.7%) | 70/384 (18.2%) |

No draws or timeouts occurred in these evaluations. This is evidence of
improvement on the tuning panel, not a completed sweep or held-out validation.
It has not surpassed the old composition's 35.2% argmax benchmark; that older
policy had a different training history and freely chosen movesets.
The [CSV](../../reports/pokemon_sweeps/20261015_vast_setup/results.csv),
[raw score summaries](../../reports/pokemon_sweeps/20261015_vast_setup/default_trial.json),
and [qualification record](../../reports/pokemon_sweeps/20261015_vast_setup/qualification.json)
are committed; full match records and checkpoints remain outside Git.
