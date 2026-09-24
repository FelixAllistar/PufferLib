# Pokémon conversion to PufferLib 5.0

The native battle engine and semantic free-pick game core are preserved from
`5c` at `036cf4251`. This is a battle simulator, not a ROM emulator; no ROM is
needed. The engine, drafting, legal masks and observations are unchanged.
The current source defines ABI 3, 168 actions and **648 observation bytes**.

```sh
make -C ocean/pokemon test
make -C ocean/pokemon sanitize
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

Not ready for training yet: the 5.0 environment adapter, semantic CPU/CUDA
network integration, viewer, state/core-reset and league/experiment workflows
still need porting. Do not substitute the generic MLP for the semantic model
or load old checkpoints through a different architecture. Full legacy source,
generator scripts and experiment documentation remain in
`archive/5c-before-unification-20260924` while these ports proceed.
