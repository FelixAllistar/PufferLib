# External model and ABI contract

The sampler accepts an immutable compiled V2 model. Input ABI and sampler ABI
are separate: `BcInput` is ABI 1 with 60 signed 64-bit fields occupying 480
bytes; the runtime/sampler ABI is 2. The public C declarations are in
`sampler/clone.h`. The bridge retains the same declarations in its separate
translation unit, with a compile-time input-size assertion.

## Model binding

The external binding JSON must contain exactly these ten fields:

| Field | Type and meaning |
| --- | --- |
| `model_version` | Integer `2` |
| `size_domain` | Integer `2` |
| `size_encoding` | String `legal_interior_fraction_f64_v2` |
| `model_domain` | String `behavior-clone-full-model-v2` |
| `model_npz_sha256` | Nonzero lowercase SHA256 of the selected NPZ |
| `model_numeric_sha256` | Nonzero lowercase canonical numeric SHA256 |
| `module_source_closure_sha256` | Nonzero lowercase SHA256 of the separately supplied standalone source closure |
| `module_binary_sha256` | Nonzero lowercase SHA256 of the separately supplied standalone sampler binary |
| `numeric_bytes` | Integer in `1..524288` |
| `nodes` | Integer in `1..4064` |

Booleans and floating-point numbers are rejected for integer fields. Duplicate
JSON keys, nonfinite constants, extra fields, and missing fields are errors.
The transport receives the six identity/count fields after the evaluator
validates the full ten-field object. The linked bridge binary is bound by a
separate SHA256; it does not replace the standalone sampler's identity.

The native metadata API reports runtime ABI 2, input size 480, model and domain
version 2, the two domain strings, feature-contract digest, numeric bytes,
nodes, and model hashes. Native model validation checks the actual compiled
arrays before any learned RNG draw. A successful compilation alone performs
none of those native checks and creates no play qualification.

## NPZ schema

The exporter accepts precisely the following arrays, with no pickled objects:

| Name | NumPy dtype | Shape |
| --- | --- | --- |
| `model_version` | `uint16` | `(1,)`, value 2 |
| `size_domain` | `uint16` | `(1,)`, value 2 |
| `roots` | `int16` | `(32,)` |
| `feature` | `int16` | `(nodes,)` |
| `threshold` | `float64` | `(nodes,)` |
| `left` | `int16` | `(nodes,)` |
| `right` | `int16` | `(nodes,)` |
| `counts` | `float64` | `(nodes, 5)` |
| `quantiles` | `float64` | `(nodes, 8)` |
| `support` | `uint8` | `(480,)` |

All floats must be finite. Counts are nonnegative. Support values are zero or
one. Each quantile row is nondecreasing and lies in `[0,1]`. The 32 trees are
disjoint, have maximum depth six, and together reach every node. A leaf has
feature `-1` and both children `-1`; internal features are in `0..63`.

The exporter uses exact hexadecimal binary64 initializers. Canonical numeric
hashing begins with `behavior-clone-full-model-v2` and a zero byte, followed by
big-endian uint64 envelope values `(2, nodes, 32, 64, 480)`. The arrays follow
in the table's order. Integers are promoted to big-endian uint64 with signed
values represented modulo 2^64; floats contribute their exact big-endian
binary64 bit pattern. The native sampler recomputes this identity.

## Legal interior sizing

For minimum legal raise-to `m` and the actor's maximum raise-to `M`, define
`L=m+1` and `U=M-1`. A strict interior action exists only when `L<=U`.
The V2 size decoder requires a finite fraction `z` in `[0,1]` and returns
`L + floor((U-L)*z + 0.5)`. A singleton interior returns `L`.

Multiplication and addition use separate binary64 operations. Compile the
sampler with `-fno-fast-math -ffp-contract=off`. There is no post-draw projection
or fallback. Class availability and geometry are checked before RNG use; an
active sample always uses the same three-draw schedule. Earlier model-format
headers have incompatible metadata and cannot silently select this decoder.

The feature-contract identity remains
`0c998b63b609e1525628b27face7cbbf97bb76396f6060874429ee1dd57cdb36`.
The ordered feature implementation, public guards, support-cell mapping,
class mask, and sampling details are the source in `sampler/clone.c`.

This package supplies no trained NPZ, generated model header, fitted numbers,
qualification document, or empirical model assessment.
