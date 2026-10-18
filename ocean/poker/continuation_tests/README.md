# Synthetic continuation checks

These tests use explicitly specified synthetic inputs. They contain no Slumbot
hand logs, learned policies, recorded performance measurements, or network calls.
Their scope is native state, retained strategy, transport, and lifecycle
correctness. They do not establish playing strength or full-game exploitability.

The package layout is:

- `test_transport.py`: matched incremental and complete-history execution, exact
  pending-request duplicates, terminal receipts, input validation, action grammar,
  and separate integer/eval7 settlement.
- `test_puffer_bridge.c` and `pufferenv.h`: the actual `poker.h` hooks compiled
  against a minimal test-only Puffer ABI. This is not the complete trainer.
- `fixtures/`: fixed synthetic decks, public river roots, and grammar cases. The
  file bytes are retained so tests can bind the intended inputs by hash.
- `oracle/`: an independent native reference and read-only retained-game probe;
  see its README for build and invocation details.

## Transport and settlement checks

From the repository root, first build the additive library:

```sh
bash ocean/poker/build_continuation.sh
```

Pass the stateless comparison library explicitly. The test does not silently
download an archived binary or select another policy. Its main workload uses
the sixteen fixed decks, each seat, and both replay modes; expected outcomes are
computed during the test rather than stored as opponent measurements.

```sh
python3 ocean/poker/continuation_tests/test_transport.py \
  --candidate build/poker/libpoker2_context.so \
  --control /absolute/path/to/stateless-control.so \
  --eval-source ocean/poker/eval.py \
  --output build/poker/continuation_checks.json
```

The Python environment needs the existing evaluator's dependencies and `eval7`
for independent showdown ranking. The control's source and binary identity are
part of any reproducibility record. A different comparison library is a different
test invocation. Generated reports belong outside this source directory.

## Minimal Puffer bridge

This explicit command enables continuation only for the heads-up hook test:

```sh
gcc -O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra -Werror \
  -Wno-unused-parameter -DPOKER_PLAYERS=2 -DPOKER_RIVER_CFR=1 \
  -DPOKER_RIVER_CONTINUATION=1 -I ocean/poker/continuation_tests \
  ocean/poker/continuation_tests/test_puffer_bridge.c -lm \
  -o build/poker/test_continuation_puffer
build/poker/test_continuation_puffer
```

The hook test includes two lanes, namespace separation, engine-copy continuity,
true resets, and policy changes. `PC_POKER_HEADER` can name another source tree's
`poker.h` for an explicitly chosen comparison. It defaults to the adjacent poker
implementation. Sanitizer flags can be supplied by the caller; proof-only stubs
must never be linked into the production library.

## Information boundaries

Own holdings remain immutable throughout a context. Opponent revelations and
true terminal payouts enter a separate settlement path only after that context
closes. Dummy-hole native settlement is not the independent payout oracle.
Scripted construction branches, where used by separate evaluator tests, must be
identified separately from native solver behavior.
