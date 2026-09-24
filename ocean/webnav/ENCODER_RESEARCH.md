# Encoder research for WebNav

Status: research snapshot, 2026-09-20. The recommendation is to keep the
environment's state transition and laws in Bend, keep a frozen semantic model
outside the hot browser loop, and test semantic features against WebNav task
success before increasing the model budget.

## What is in the current table

The installed asset is `minishlab/potion-base-8M` at the pinned revision in
`TEXT_ENCODER.md`. It is not a dictionary of website examples and it was not
trained in this repository. It is a static embedding matrix: one learned
vector per tokenizer entry. The model's documented provenance is:

1. Distill a sentence-transformer teacher (`BAAI/bge-base-en-v1.5`) into one
   vector per token.
2. Create sentence-level teacher targets from C4 text and train the static
   table with Tokenlearn.
3. Apply PCA and smooth inverse-frequency weighting.

The upstream explanation is in [Model2Vec's introduction](https://minish.ai/packages/model2vec/introduction/), and the model family is listed in the
[Model2Vec repository](https://github.com/MinishLab/model2vec). Our copy is
29,528 tokens by 256 `float32` values: 7,559,168 parameters and 30,236,760
bytes in the weights file. `text_encoder.c` reproduces the pinned tokenizer
profile and normalized pooled lookup in C; it does not run the teacher or
train anything at inference time.

The current 16/24 nearest-label diagnostic is useful as a failure detector,
not an accuracy estimate. Passing 1,120 tokenizer/vector parity cases proves
that our native implementation agrees with the pinned asset. It does not prove
that the asset understands browser instructions.

## What can be made larger

The Potion family gives a useful controlled size ladder. The dimensions and
weight sizes below come from each model's checked-in `config.json` and
`model.safetensors` metadata; the approximate quantized sizes are arithmetic,
not measurements of a particular quantizer.

| Asset | Vocabulary | Output dimension | F32 matrix (decimal MB) | F16 matrix (decimal MB) | Int8 matrix (decimal MB) | Use |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `potion-base-2M` | 29,528 | 64 | 7.56 | 3.78 | 1.89 | smallest screen |
| `potion-base-4M` | 29,528 | 128 | 15.1 | 7.56 | 3.78 | cheap screen |
| `potion-base-8M` | 29,528 | 256 | 30.2 | 15.1 | 7.56 | current baseline |
| `potion-base-32M` | 63,091 | 512 | 129 | 64.6 | 32.3 | highest English general model |
| `potion-retrieval-32M` | 63,091 | 512 | 129 | 64.6 | 32.3 | retrieval-oriented variant |
| `potion-multilingual-128M` | model-specific | 256 | about 512 | about 256 | about 128 | 101 languages; defer |

The 32M model is the interesting upper bound for the first comparison. The
upstream results report MTEB averages of 52.13 for `potion-base-32M`, 51.08
for `potion-base-8M`, and 55.93 for `all-MiniLM-L6-v2`. Those numbers are from
the upstream MTEB experiment, not WebNav, and the same page reports a much
larger gap on retrieval (32.67 static versus 42.92 MiniLM). See the
[primary Model2Vec results](https://minish.ai/packages/model2vec/results/).
The result is enough to justify testing 32M; it is not enough to assume that
it will solve negation, numeric comparison, or browser action selection.

The 32M table doubles the per-token vector width and has over four times the
weights of the current table. A full `128 nodes x 512 float32` observation is
256 KiB before policy features. At `1,024 agents x 32 rollout steps`, retaining
all such vectors would be about 8 GiB. The implementation should therefore
keep exact text/token IDs and immutable shared vectors in the observation or
cache, and gather/project only the vectors needed by the policy. An int8 table
may be viable, but it must be validated for WebNav accuracy and dequantization
cost before being treated as a free 4x memory reduction.

Model2Vec's current documentation explicitly supports sequence embeddings,
optional dimensionality reduction, float16/int8 quantization, and vocabulary
quantization. `encode_as_sequence` is especially relevant here: it returns one
static vector per token rather than forcing a permutation-invariant mean. See
the [upstream inference documentation](https://minish.ai/packages/model2vec/inference/)
and the [Rust implementation's documented formats](https://github.com/MinishLab/model2vec-rs#features).
The Rust implementation is useful as a parity reference, but adding Rust is
not necessary for the WebNav runtime; the table format and tokenizer can be
implemented behind the existing native bridge.

## Why the current pooled vector fails

Mean pooling is deliberately cheap, but it is invariant to token order. The
strings `translate from English to French` and `translate from French to
English` contain the same token multiset and therefore have exactly the same
pooled vector when the same static token vectors and weights are used. No
larger static table can recover information that pooling discards.

Negation is a softer failure: `delete the message` and `do not delete the
message` do not have the same multiset, but the pooled vector has no rule that
requires `not` to reverse an action. Numeric meaning is another separate
problem. A vector for `19` can be near a vector for `20` without exposing the
ordered relation `19 < 20`; token IDs themselves are labels and their numeric
distance has no semantic meaning.

These are architectural limitations, not bugs in Potion. The original native
diagnostic already observes all three kinds of errors. The policy input must
retain complementary exact and structured channels:

* ordered token IDs or per-token static vectors, with a small positional or
  recurrent/attention-like policy-side mixer;
* exact normalized name, label, placeholder, value, and instruction matches;
* parsed signed integers/floats, currency, units, and comparison direction;
* explicit flags for negation, `all/any/none`, `before/after`, and common
  action words, with the matched token span retained;
* role, state, geometry, parent/label relations, and occurrence-specific DOM
  identity.

The first three are deterministic features and can be specified as Bend data
and laws. A frozen vector is a useful semantic hint, not the authority for an
action. In particular, a nearest cosine candidate must never be allowed to
override an exact ID, number, polarity, or role constraint.

## Contextual reference model

Use `sentence-transformers/all-MiniLM-L6-v2` as an offline reference, not as
the default per-node environment encoder. Its model card describes a 384
dimensional sentence vector and Apache-2.0 licensing. The repository exposes
ONNX exports: the normal model is 90.4 MB, the graph-optimized O4 export is
45.2 MB, and the AVX2/AVX512 int8 exports are about 23 MB. The model has
22,713,216 F32 parameters according to the Hugging Face model metadata. See
the [model card](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2),
the [ONNX file listing](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/tree/main/onnx),
and the [Hugging Face API metadata](https://huggingface.co/api/models/sentence-transformers/all-MiniLM-L6-v2).

ONNX Runtime exposes a native C API and a thin C++ wrapper, so this reference
does not require a Python runtime in production. See the [official C/C++ API
documentation](https://onnxruntime.ai/docs/api/c/c_cpp_api.html). It still
requires a tokenizer compatible with the model and a substantially heavier
runtime than a lookup table. We should measure it on the target CPU rather
than importing a published speed claim.

The practical uses are:

1. Encode each task instruction once at reset and cache the whole-string
   result.
2. Encode a changed control name/value only when the DOM mutation changes it.
3. Use the contextual result offline as a teacher to label or rank generated
   instruction/control pairs.
4. Compare a static policy against the contextual reference on the same
   episodes.

Running MiniLM over 128 nodes on every environment step would move the
semantic model into the training bottleneck and defeat the point of a fast
environment. A small contextual model can still be valuable as a slow
evaluation lane or as a source of a better static table later.

## Proposed evaluation, in order

Do not choose the largest table by MTEB score alone. Build a fixed WebNav
semantic suite from the real task language and generated controls, with no
template leakage between train and test. Each example has a task instruction,
candidate node text, the correct node or a `none` answer, and typed metadata.
Include these disjoint slices:

1. exact and paraphrased action/name matches;
2. word-order pairs, including translation direction;
3. negation and polarity pairs;
4. numeric equality, ordering, ranges, currency, decimals, and IDs;
5. distractors sharing nouns but differing in role or state;
6. Unicode, punctuation, typos, casing, and long labels;
7. repeated labels with geometry/tree/relationship disambiguation;
8. degraded observations with missing ARIA names or labels.

For every encoder candidate, report:

* top-1 and top-k node selection, `none` accuracy, and calibrated rejection;
* each slice separately, especially order/negation/numeric accuracy;
* bytes loaded, resident memory, cold load time, live p50/p95/p99, and cache
  hit/miss latency for 1, 32, 128, and 512 strings;
* policy success, episode length, and invalid-action rate when the same frozen
  policy architecture receives the candidate features;
* exact parity against the upstream tokenizer and vector reference;
* behavior under byte-budget and observation-budget limits.

The initial matrix should be:

| Candidate | Purpose |
| --- | --- |
| exact/token/character features | lower-bound lexical baseline |
| Potion 2M/4M/8M | size and latency curve |
| Potion 32M | larger static upper bound |
| Potion retrieval 32M | query-to-control retrieval comparison |
| static features plus structured channels | likely deployable controller |
| MiniLM-L6-v2 ONNX | contextual quality reference / slow lane |

The decisive metric is task success with controlled ablations: pooled static
only, pooled plus exact/typed channels, sequence static plus a tiny policy
mixer, and contextual reference. If structured channels close the gap to the
contextual model, keep the fast architecture. If they do not, use the
contextual model to generate supervision or investigate a domain-distilled
static table rather than silently enlarging the pooled vector.

## What “all Bend” can and cannot mean here

The environment contract, state transition, reward/deadline arithmetic, and
invariants should remain in Bend. That is the part where the laws provide a
reviewable correctness boundary. The current tokenizer, UTF-8/ICU normalizer,
Safetensors loader, and matrix math live in C because the generated Bend
runtime has no equivalent standard text/model loader and the browser bridge is
already native C.

The static model itself is an external frozen artifact, not Bend source. It
can be embedded or loaded by a C bridge and exposed to Bend as immutable
features. A strict all-Bend implementation is possible only after deciding on
a Bend byte-string/Unicode contract and implementing a bounded tokenizer and
matrix lookup there; that would change the proof and memory work without
addressing the semantic limitations above. Keep the semantic interface and
its safety laws in Bend, and treat the native encoder as an interchangeable,
parity-tested feature provider.

## Recommendation

Do the next experiment with no model migration: add exact ordered token
features, typed numeric/polarity features, and an optional per-token Potion
sequence view alongside the existing 256-dimensional pooled vector. Run the
semantic suite and a small policy ablation first. In parallel, benchmark
Potion 32M and MiniLM ONNX only as reference candidates. Promote Potion 32M
to the default only if its WebNav slice accuracy improves enough to justify
the 4x weight footprint; promote MiniLM into training only if its measured
task-success gain survives the real CPU latency and memory budget.

This keeps the high-speed CPU environment intact while making the hard
semantic question measurable. The table can become larger later; the lost
order, polarity, and numeric structure must be represented explicitly now.
