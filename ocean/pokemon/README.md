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

Not ready for training yet: semantic CUDA network integration, native adapter
setup callbacks, state/core-reset and league/experiment workflows still need
porting. `config/pokemon.ini` preserves the old profile for evaluation and is
not a working 5.0 training config. Generic native/CPU trainer entry points
explicitly fail during this stage. Do not substitute the generic MLP for the semantic model
or load old checkpoints through a different architecture. Full legacy source,
generator scripts and experiment documentation remain in
`archive/5c-before-unification-20260924` while these ports proceed.
