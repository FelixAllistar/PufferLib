# Optional heads-up clone

This package contains a standalone C sampler, a native/Puffer bridge, public
transport validation, and an actual-hand evaluator. Model data is an explicit
external input. Importing the Python modules performs no HTTP requests.

The existing poker build, simulator header, and optional retained-river build
remain separate. Nothing in this directory is enabled by `build.sh` or
`build_continuation.sh`. The clone entry has no combined river-CFR mode.

## Layout and boundaries

| Path | Responsibility |
| --- | --- |
| `bridge/` | Public hand lifecycle, current-actor input projection, original-policy fallback, and strict response receipts |
| `sampler/clone.c` | Versioned feature extraction, immutable compiled-model checks, support lookup, and the three-draw sampler |
| `sampler/export_model.py` | Lossless export of an explicitly selected, already fitted V2 NPZ into a generated C header |
| `build.py` | Local base-source materialization and separate native/sampler compilation |
| `prepare_puffer_source.py` | Exact source-text substitutions that produce the additive Puffer header |
| `evaluation/` | Fixed-quota execution, complete actual-policy replay, independent payout checks, and source-bound orchestration |
| `evaluation/fixtures/` | Literal synthetic deck inputs; no recorded hands or learned data |

The bridge supplies the model with the current actor's own two cards, the
currently visible board, public money and action state, and the last four
actual public actions translated relative to that actor. The native state is
not passed into the model. Hidden opponent cards, future board cards, payoffs,
and response-only private receipts have no model-input fields.

A clone decision requires heads-up play, policy 7, the default sample setting,
no supplied profiles, initial stacks of 20,000, a supported cell, and the
actor's first decision on the preflop, flop, or turn. Inactive cases enter the
original policy before any clone RNG draw. A clone hit consumes exactly three
draws. Protocol errors terminate the context instead of invoking a fallback.

## Required external inputs

The build requires all of these explicitly:

1. A generated V2 model header and its SHA256.
2. A JSON object containing the exact ten model-binding fields described in
   [MODEL_FORMAT.md](MODEL_FORMAT.md), and that file's SHA256.
3. The matching standalone sampler binary, whose hash is in the binding.
4. The matching standalone source-closure file, whose hash is in the binding.
5. Local access to the six native/Puffer files at Git commit
   `a4bfd971ae4bdc2c46b8bffef2617f368b9be017` and the separately reviewed
   Python `eval.py` at `8ae0b7ed8742a25c89b29a382eaad23442c57120`, or an
   explicitly selected export containing those exact seven files.

The standalone binary and source-closure file are qualification references.
The build hashes them as opaque files and does not load them or follow paths
inside the closure. It preserves their identities in the new build manifest.
Compilation does not create predictive, implementation, or playing
qualification for the supplied model.

To create the header from an already fitted model, the optional exporter needs
Python and NumPy. It verifies the caller's NPZ SHA256, the exact V2 array
schema, graph, and numeric constraints, then writes unchanged numeric values
as hexadecimal floating-point C initializers:

```sh
python3 -B ocean/poker/clone/sampler/export_model.py \
  --model /absolute/private/full_model.npz \
  --expected-npz-sha256 MODEL_NPZ_SHA256 \
  --output /absolute/private/bc_model_generated.h \
  --receipt /absolute/private/export.json
```

The exporter performs no fitting or prediction. Its output and receipt must
be new paths. Model headers, NPZ files, binary libraries, execution records,
and qualification documents stay outside this source directory.

## Explicit native build

The supported compilation profile uses GCC 13.3.0. The base/native object uses
`-O3 -march=native -ffp-contract=off -std=c11 -Wall -Wextra`, two players, the
original default batch of 64, and no `-DNDEBUG`. The sampler is a separate
object with `-fno-fast-math` placed before `-ffp-contract=off`, followed by
`-std=c11 -Wall -Wextra`. The objects are linked without LTO. Compiler processes
discard ambient include, compiler, linker, loader, and flag overrides; the
fixed path, locale, and thread settings are recorded in the build manifest.
Other compilers or architectures need their own fallback and arithmetic
qualification; selecting them is not implicit in this entry.

From the repository root, choose a new build output directory:

```sh
python3 -B ocean/poker/clone/build.py \
  --repository . \
  --header /absolute/private/bc_model_generated.h \
  --expected-header-sha256 HEADER_SHA256 \
  --model-binding /absolute/private/model_binding.json \
  --expected-model-binding-sha256 MODEL_BINDING_FILE_SHA256 \
  --module-binary /absolute/private/standalone_clone.so \
  --module-source-closure /absolute/private/standalone_source_closure.json \
  --output build/poker/clone-optional
```

The command uses local `git cat-file blob` reads and verifies every base file
against `base_sources.json`. It never fetches, checks out, or changes a Git
reference. A source archive without Git can instead pass
`--base-source /absolute/pinned-source-export`; that directory must contain the
six a4b native/Puffer files at its root and the separately pinned
`evaluation_support/eval.py` below it. An unavailable or different source is
an error. The newer Python support supplies the reviewed terminal-validation
API; it does not change the a4b C fallback.

The isolated output contains `reference_a4b/`, `evaluation_support/`, the staged
`source/` and `sampler/`, the explicitly supplied header under `private/`, two object files,
and `libpoker2_clone.so`. Existing relative C includes are preserved. The build
also produces the source-checked `source/poker_clone.h` and records file and
compiler identities in `build_manifest.json`. It never loads the resulting
library or samples a policy. A build failure preserves its output; the command
does not replace an existing build or retry automatically.

## Puffer integration

An explicitly selected Puffer translation unit may include the generated
`source/poker_clone.h`, compile with `POKER_CLONE_BRIDGE=1` and two players,
and link the separate `objects/clone.o`. It must supply Puffer's normal
`pufferenv.h` dependency and use the native compilation profile above. Do not
also link `objects/bridge.o`: the Puffer header defines its own bridge entry
points in that translation unit.

After `my_vec_init` and before the first reset, call
`bb_puffer_configure_vector` with the explicit model binding and a unique
32-byte job/worker/vector namespace. The hooks reserve a generation before
each true deal, start after the deal, observe learner and opponent actions,
and close before storage is freed. An unchanged replay rebuild is distinct
from a new deal. Disabled or non-heads-up hook expansions retain the original
calls and need no sampler object.

The generated header is an opt-in integration surface. This package does not
modify the repository's existing Puffer selection or imply a full PPO build.

## Offline integration plan

The plan creator uses the standard library. Supply the new build manifest and
an independently selected original-policy control library explicitly:

```sh
python3 -B ocean/poker/clone/evaluation/create_synthetic_plan.py \
  --build-manifest build/poker/clone-optional/build_manifest.json \
  --expected-build-manifest-sha256 BUILD_MANIFEST_SHA256 \
  --control /absolute/original-policy.so \
  --expected-control-sha256 ORIGINAL_LIBRARY_SHA256 \
  --run-id clone-offline-check \
  --output build/poker/clone-offline-plan
```

The control must be the independently bound original a4b policy used for the
comparison and revealed-card settlement. A file hash alone does not establish
its provenance. The creator hashes the complete listed source and artifact
inputs, creates a synthetic plan, and prints its identity. It performs no
native calls and does not launch that plan.

The separately invoked offline pipeline requires Python, `requests` for the
socket-free redirect adapter, and `eval7` for independent showdown ranking:

```sh
python3 -B ocean/poker/clone/evaluation/test_clone_pipeline.py \
  --plan build/poker/clone-offline-plan/synthetic_plan.json \
  --expected-plan-sha256 SYNTHETIC_PLAN_SHA256 \
  --output build/poker/clone-offline-output
```

Its fixed workload is four literal decks in both seat orientations for each
policy arm: 16 primary hands. Two additional jobs inject a lost action response
and an HTTP redirect through a socket-free adapter. Those jobs must stop after
one applied action without retrying the wager or replacing the interrupted
reservation. Every actual policy decision is replayed. Revealed-card settlement
runs after contexts close, with independent integer contributions and `eval7`
ranking. The synthetic report cannot assert playing strength.

The separate source-only contract and preflight tests are
`evaluation/test_clone_contract.py` and `evaluation/test_clone_preflight.py`.
The latter requires a new `--fixture-root` directory for its fabricated
metadata. It forbids native-library and network operations.

## Explicit live execution

The copied evaluator retains its V2 source-bound live qualification and
allocation contract. Only its explicit `clone_series.py run` entry accepts a
matching live plan, completed qualification document, and authorization bound
to one output directory. Neither the build manifest nor the generated
synthetic plan supplies those documents. A new model or changed qualification
protocol requires a separately reviewed plan/version.

Actual-hand raw return is the primary metric. The clone evaluator contains no
river first-divergence baseline. Each hand is durably reserved before its
request, uncertain requests are abandoned without retry, and the complete
planned series is required for analysis. The optional retained-river evaluator
remains in its existing directory with its own analysis contract.

Source provenance is described in [SOURCE_PROVENANCE.md](SOURCE_PROVENANCE.md).
The repository's existing licenses and third-party notices continue to apply.
