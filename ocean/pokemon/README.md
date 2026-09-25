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
named expert banks are explicitly blocked pending their loader port;
full league/experiment workflows
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
