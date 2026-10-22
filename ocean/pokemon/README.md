# Pokémon conversion to PufferLib 5.0

## Manual training with live Metamon scores

Normal `./puffer train` now evaluates saved snapshots every 10% of the requested
training budget and displays Metamon **latest**, **best**, W/D/L, game count and
evaluation progress inside the ordinary PufferLib dashboard. No sweep wrapper or
separate status window is needed. It briefly waits during each evaluation and
then resumes the same optimizer/RNN training state. Native training's `Evaluate`
timing and mixed `score` remain separate from the explicitly labeled Metamon score.

```bash
./puffer train
# Quick interim checks: 2 games/team = 6 total. Final count is separate.
./puffer train --metamon.games=2
# Your usual manual training overrides still work; choose the architecture explicitly.
./puffer train --policy.hidden_size=1024 --policy.num_layers=2 --metamon.interval_fraction=0.1
# Training without the benchmark:
./puffer train --metamon.enabled=0
```

This uses the existing CUDA evaluator environment
`build/pokemon/metamon-eval-env-cu128/bin/python`; install it with
`bash ocean/pokemon/benchmarks/setup_private.sh cu128` if absent. Configure
`metamon.python`, `device`, `games` and `final_games` (per each of three teams), `interval_fraction`,
`seed`, and `timeout` in `config/pokemon.ini`. Currently one training GPU is
supported for live evaluation. Architecture, step budget and frozen-opponent
settings are not changed by enabling evaluation. Existing settings without a
`[metamon]` section keep live evaluation disabled for compatibility.

Scores are wins plus half draws against sampled TaurosV0, with an argmax learner,
balanced seats and fixed seeds. 24 games is a noisy screen, not confirmation of
strength; selecting the best of multiple snapshots adds selection bias. All
evaluated checkpoints are retained. Each has `<checkpoint>.metamon/summary.json`,
per-game outcomes, native `progress.txt`/`result.txt`, and an adjacent `.metamon.log`.
`env/metamon_score`, `env/metamon_best_score`, and their checkpoint step counts are
also included in native logs. Pending evaluations are not displayed as zero.
Failures are explicit and preserve checkpoints; a failed final evaluation never
substitutes mixed training score for a benchmark result. The evaluator exits
when its native trainer exits (Linux parent-death guard).

Oak remains available as a separate opt-in benchmark; Metamon takes precedence
when enabled. Native eval-only checkpoints do not change the historical-opponent
rotation pool. No training or sweep is automatically started by installation.

The current `./puffer sweep` defaults run a fresh 500-trial, 25-dimensional search:
25M–500M steps, widths 64/128/256, and 1–3 layers. Interim checks use 6 games and
final checkpoints use 72; only finals rank trials. See
[the broad-sweep record](../../reports/pokemon_experiments/20261019_broad256/NOTES.md)
for all ranges, encoder checks and caveats. A 2 GiB disk reserve prevents new
trials from starting when space is low; no retained artifacts are deleted.

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

League training defaults to `oak.enabled=0`, including states created before
the Oak hook existed; end-of-round tournaments run only with `--evaluate`.
Creation overrides may explicitly opt into Oak with `oak.enabled=1`. Fixed-set
seeds are supported through a custom roster entry such as
`"team":"catalog:421,366,389,395,512,445","lead":121`; native manifests preserve
the exact ordered sets. Species-constrained peers continue learning moves freely.

The current BF16 experiment, its complete baseline history and checkpoint hashes,
the 14-member starting roster, and the 50-round/50M-stage plan are recorded in
[the October 17 experiment notes](../../reports/pokemon_experiments/20261017_roundrobin_50m/NOTES.md).
It uses a hash-verified snapshot of the existing BF16 executable; the older
FP32 build example below is a separate precision choice.

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

The native Protein sweep targets 500 completed trials, default settings first
for a new history. No short
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

Completed trials are appended durably to `saved/pokemon/starmie_sweep/sweep.history`.
Running the same command again reloads their scores and hyperparameters into
Protein, refits its search model, and continues toward the total trial target.
The history is locked to one sweep writer, validates parameter names/ranges,
and the launcher checks that fixed training and evaluation settings still match.
This preserves observations; it does not restore the exact previous internal
random-number or GP optimizer state. Only completed final evaluations count.

For logs produced before durable history was added, import once before resuming:

```bash
python3 ocean/pokemon/sweep.py import-history
python3 ocean/pokemon/sweep.py run
```

The original 16 trials were imported and count toward the 500 total. Their best
argmax result was trial 15: 177 wins and 207 losses out of 384 games (46.1%),
with sampled score 31.0% (117 wins, four draws, 263 losses). All 16 checkpoints
and complete match records were also copied off the GPU host locally under
`saved/pokemon/vast_sweep_20261015/`. Import checks completed training budgets,
evaluation counts, fixed settings and checkpoint hashes before accepting scores.

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

The completed 500-trial FP32 sweep selected `sweep_1792079639324_0223` by the Oak
argmax objective: 233 wins, two draws and 149 losses (60.9375% score). The
[winning native configuration](../../reports/pokemon_sweeps/20261016_winner/winning_trial.ini)
and [evaluation summary](../../reports/pokemon_sweeps/20261016_winner/summary.json)
retain the full experiment setup and checkpoint hash. The default config keeps
its fixed team, frozen opponents, H64/L2 architecture, 16,384 agents,
two buffers and 25,165,824 learner steps.
The winning checkpoint is backed up locally
under `saved/pokemon/vast_sweep_20261015/checkpoints/sweep_1792079639324_0223/`.

For the BF16 experiment, build the normal executable with `./build.sh pokemon`
(without `--float`), then use `./puffer train` or `./puffer sweep`. The environment
is compiled in; neither command takes a `pokemon` positional argument. Precision
is a build setting, not an INI setting. The historical FP32 score is not a
reproduced BF16 result.

`config/pokemon.ini` explicitly overrides every generic sweep range. The BF16
sweep keeps the same fixed experiment and ten bounded PPO search dimensions,
targets 500 completed trials, and evaluates each final checkpoint with Oak.
Its append-only history is `saved/pokemon/starmie_sweep/bf16_sweep.history`,
separate from the FP32 sweep's `sweep.history`; rerunning the same command resumes
that history. The dashboard stays live. Failed trials are recorded and the sweep
continues, stopping after 32 worker failures in one invocation. Keep this history
paired with the BF16 build; the older `sweep.py run` command above uses the
historical FP32 profile and binary instead.

The selected BF16 trial is `sweep_1792195882955_0163`: 212 wins, two draws and
170 losses against Oak (55.46875% argmax score). `config/pokemon.ini` uses its
exact saved PPO settings for ordinary `./puffer train`, starting fresh with the
same seed and full trial budget. The selected checkpoint, saved configuration,
Oak summary and BF16 executable are backed up locally under
`saved/pokemon/bf16_best_1792195882955_0163/`. The matching executable SHA-256 is
`07849fd021b6534648cdbcddf98375ae855a9e31c12b5f9829c607d0a2f93a78`.

### Player-information-only external evaluation (experimental)

This is a separate protocol from the perfect-information Oak panel above.
`benchmarks/evaluate_private.py` gives the external opponent only its own
648-byte native player observation: its own team and public opponent reveals,
not the learner's hidden team/moves or raw engine state. It does not change
the training binary, observation ABI, running league or automatic evaluations.

Two opponents are currently available:

- `heuristic`: our own simple type/STAB/power/accuracy rule, for checking the
  adapter. It is not an imported competitive bot or a meaningful strength target.
- `TaurosV0`: released Metamon Gen1 pretrained weights (checkpoint 62), using
  upstream PyTorch inference in an isolated evaluation environment. Our learner
  still uses the existing native C inference path. The torch SDPA backend
  preserves the published causal `(32, 0)` window and BF16 Q/K/V rounding;
  loading requires an exact checkpoint hash and strict parameter match.

```bash
# Evaluation-only setup; does not install into the trainer's environment.
bash ocean/pokemon/benchmarks/setup_private.sh
python3 ocean/pokemon/tests/test_private_view.py
build/pokemon/metamon-eval-env/bin/python ocean/pokemon/tests/test_metamon_projection.py

# --games is games PER opponent team; --mode controls OUR policy's actions.
# TaurosV0 samples its policy. Output directories must be new.
OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=1 \
  build/pokemon/metamon-eval-env/bin/python \
  ocean/pokemon/benchmarks/evaluate_private.py CHECKPOINT.bin \
  --opponent TaurosV0 --mode argmax --games 8 --threads 2 \
  --output NEW_OUTPUT_DIRECTORY

# On the Vast RTX 50-series host: isolated CUDA 12.8 environment, GPU inference.
bash ocean/pokemon/benchmarks/setup_private.sh cu128
OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=1 \
  build/pokemon/metamon-eval-env-cu128/bin/python \
  ocean/pokemon/benchmarks/evaluate_private.py CHECKPOINT.bin \
  --opponent TaurosV0 --device cuda --mode argmax --games 128 --threads 2 \
  --output NEW_OUTPUT_DIRECTORY
```

The default panel is the same three pinned teams used for Oak, balanced across
player seats, but scores against different bots must not be mixed. Use a small
panel to validate the adapter, then more games and independent seeds for ranking
policies; game count is configurable per invocation. Reports record checkpoint,
config, adapter, source and team hashes. Completed games are saved incrementally.

This is a **native-engine adaptation**, not a verified full Showdown bot port
or a ladder rating. Public HP is quantized to 255; opponent PP/effect order is
reconstructed from compact observations rather than a complete protocol stream.
The final Metamon probabilities are conditioned on legal native actions because
upstream probability clipping otherwise gives masked actions a small positive
probability. No random invalid-action fallback is used. Further qualification
is needed before making external playing-strength claims or using it as a sweep
objective. Keep Oak as a separately labeled perfect-information stress test.

### Match evaluation effort to the experiment

Oak game count is already a per-run setting: `--oak.games=N` means N games
against each of three teams, separately for argmax and sampled actions (6N games
total). The default is 128 games/team, search budget 4096 and six workers.
Choose evaluation effort for the question being tested:

- Coarse sweep screening: `--oak.games=64`; noisy results are suitable for
  discarding clearly weak candidates, and finalists need confirmation.
- Routine sweeps and longer-training probes: 128 games/team, the default.
- Small improvements or final comparisons: `--oak.games=1024`; 3,072 games per
  mode gives roughly a 1.8 percentage-point 95% sampling margin near a 50% score.
- Training-throughput checks: `--oak.enabled=0`. Oak-scored sweeps require Oak
  evaluation and must not use training win rate as a replacement objective.

Keep search budget, team panel and game count consistent within a comparison.
Changing `oak.budget` changes the strength of the opponent; changing `oak.games`
changes the precision of the score. Confirm selected models on additional games
and new battle seeds to reduce selection bias. Oak's internal search randomness
is not controlled by the recorded battle seed. Repeated evaluations of the same
byte-identical trial-163 model scored 55.47%, 50.78%, and 52.73%; the last used
1,024 games/team. The detailed baseline is on the GPU host under
`logs/pokemon/1792196525320.ini.oak-1024/`.

Choose these overrides for each experiment rather than raising the default
evaluation cost for every training run. Six workers can run all six team/mode
jobs concurrently; simultaneous evaluations share the host's CPU quota.

### Check whether more training still helps

The selected model received 33,554,432 counted steps (25,165,824 learner steps).
A useful first probe is another equal-length stage against the same frozen
opponents, followed by routine Oak evaluation. Loading a checkpoint restores
weights only: optimizer state, recurrent state, step counter and learning-rate
schedule restart. This is a continuation stage, not an exact optimizer resume.

```bash
# Add 33,554,432 steps to the measured baseline: 2x its cumulative training.
./puffer train \
  --base.load_model_path=checkpoints/pokemon/1792196525320/0000000033554432.bin \
  --train.total_timesteps=33554432

# Alternative experiment: a fresh 10x run with one longer annealing schedule.
./puffer train --train.total_timesteps=335544320

# Confirm a promising completed checkpoint without retraining it.
build/pokemon/oak-benchmark-env/bin/python \
  ocean/pokemon/benchmarks/evaluate_checkpoint.py CHECKPOINT.bin \
  --output NEW_OUTPUT_DIRECTORY --games 1024 --budget 4096 --workers 6 \
  --seed 20261017
```

Track the loaded checkpoint and cumulative steps for each continuation stage.
For 2x -> 4x -> 8x cumulative training, add 1x, then 2x, then 4x stages, loading
the preceding stage's final checkpoint each time. Confirm promising gains before
changing the opponent curriculum. The dashboard's `root_score` mixes current
self-play and frozen-opponent games, so it does not establish that the expert
pool is solved. If direct evaluations against the frozen pool saturate while
Oak progress stalls, updating opponents through round-robin league training is
a reasonable next experiment.
