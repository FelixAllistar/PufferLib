# Source provenance

The original-policy fallback is pinned to Git commit
`a4bfd971ae4bdc2c46b8bffef2617f368b9be017`. Its exact source identities are in
`base_sources.json`. The required files are `native.c`, `core.h`, `rules.h`,
`equity.h`, `fullhouse.h`, and `poker.h`, all from `ocean/poker/` at that commit.
Base source is materialized only into an isolated build directory.
The build does not substitute the current working-tree simulator.

The Python evaluator support is separately pinned to `ocean/poker/eval.py` at
`8ae0b7ed8742a25c89b29a382eaad23442c57120`, SHA256
`9f6c584e0ab76c37d7df07fccefd1b3889769e4918c418a829ef4e7fc3353137`.
It is materialized under `evaluation_support/` because the unchanged clone
runner requires its `verify_terminal` API. None of that commit's C simulator
files is used for the original-policy fallback.

The bridge and sampler use separate translation units. The bridge preserves
the original native include and fallback call; the sampler exposes a versioned
480-byte input API and receives no simulator pointer. The Puffer source is
generated from the pinned `poker.h` with exact occurrence-checked substitutions.
Its generated SHA256 is recorded alongside the base sources and checked before
compilation. The repository's existing Fullhouse loader notice is at
`../third_party/fullhouse-LICENSE.txt`.

The actual-hand evaluator is independent of the optional retained-river
evaluator. Its raw-return analysis, response validation, independent payout
replay, and qualification protocol are preserved as source. Changes to one
optional integration do not select or change the other.

`package_sources.json` lists the source bundle's exact file hashes. Generated
build manifests record those sources, materialized base files, externally
supplied model identities, compiler arguments, object files, and the final
linked library. Those generated manifests are local build artifacts, and they
do not certify a trained model's quality.

This source bundle contains implementation, documentation, and literal
synthetic test inputs. It contains no learned model payload, recorded poker
hands, private execution authorizations, empirical reports, or benchmark
findings.
