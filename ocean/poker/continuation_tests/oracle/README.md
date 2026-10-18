# Retained-river native oracle source

This directory contains tests for public-tree retention, legal transitions, exact averaged rows, sampling and request lifecycle. It includes no measured results or playing evidence. The finite-game reference uses the unchanged native and CFR headers in `ocean/poker`; the continuation implementation is included separately by the instrumented test library.

## Inputs and paths

| Test source | Dependency mapping |
|---|---|
| `reference.c` | Includes `../../native.c`; exposes a public-game reference and an independent native transport wrapper |
| `candidate_probe.c` | Includes `../../native.c`, then `../../river_continuation_impl.h` with proof-only count/failure/branch hooks |
| `sanitizer_native.c` | Includes the local `candidate_probe.c` for a compact arithmetic/lifecycle sanitizer executable |
| `lsan_stub.c` | Optional test-only LeakSanitizer exit-pass stub; never link into production |
| `native_oracle.py` | Accepts explicit reference, production, probe, original-control and output paths; default synthetic roots are `../fixtures/river_roots.json` |

The six synthetic root bytes are checked against SHA256 `ecacc4a32ceaf7070be46ba4c59d17d69587ce1cc999ccf53f8bf061ce80353d`. The original policy control is an explicit input with SHA256 `b74b44f37a33736c685a2da68d3815f25a65c2dee0fc9cebdfcd7b6cbd11af4a`; it must expose the HU `poker_decide` ABI. This identity corresponds to the original control source at commit `a4bfd971ae4bdc2c46b8bffef2617f368b9be017`. The test does not fit or select a control.

## Building

From this directory, use the same compiler settings for the reference, production and probe libraries. Choose an empty output directory outside the source tree. For example, after replacing `/absolute/test-build` with the chosen directory:

```sh
gcc -O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra -Werror -DPOKER_PLAYERS=2 -DPOKER_RIVER_CFR=1 -shared -fPIC reference.c -lm -o /absolute/test-build/reference.so
gcc -O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra -Werror -DPOKER_PLAYERS=2 -DPOKER_RIVER_CFR=1 -shared -fPIC ../../continuation.c -lm -o /absolute/test-build/production.so
gcc -O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra -Werror -DPOKER_PLAYERS=2 -DPOKER_RIVER_CFR=1 -shared -fPIC candidate_probe.c -lm -o /absolute/test-build/probe.so
```

Run the Python test with explicit artifact locations:

```sh
python3 native_oracle.py --reference /absolute/test-build/reference.so --candidate /absolute/test-build/production.so --probe /absolute/test-build/probe.so --control /absolute/original-control.so --output /absolute/test-build/native-proof.json
```

`--fixtures` can explicitly name the checked synthetic-root JSON file. The output path must not already exist. Python uses only its standard library; the test source itself does no executable work on import.

## Scope

The test compares every node/edge and all stored averaged bytes in six fixed public games; every holding row is sampled at five fixed RNG seeds. It independently implements HU integer transitions, CDF/RNG arithmetic and canonical hash serialization. Production root decisions and interleaved later decisions are unforced. Separate proof-only local-draw overrides construct all-in and off-menu lifecycle cases; they advance actual RNG state but are not observations of natural policy action frequency. Faults and fixtures are fixed in source, with no alternative successful hand search.

These checks establish finite supplied-prior/action-abstraction behavior and lifecycle properties. Their certificate is not unrestricted-poker exploitability or an opponent win-rate estimate. Source changes to the base solver, fixture contract, instrumentation or model require reviewing those expectations again.

## Compact sanitizer target

```sh
gcc -O1 -g -march=native -ffp-contract=off -std=c11 -Wall -Wextra -Werror -DPOKER_PLAYERS=2 -DPOKER_RIVER_CFR=1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all sanitizer_native.c -lm -o /absolute/test-build/sanitizer-native
```

For a sandbox that cannot perform LeakSanitizer's process-inspection exit pass, the test executable may additionally link `lsan_stub.c` and run with leak detection disabled. This does not disable AddressSanitizer or UBSan, but no leak-detection claim is then supported. Production must not contain this stub, the `probe_` functions or `pc_test_` exports.
