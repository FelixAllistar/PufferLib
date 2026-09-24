# Encoder comparison

This is an offline semantic-ranking experiment. It does not run a WebNav
environment, train a policy, or change the production encoder/profile. The
harness is [tools/encoder_compare.cjs](tools/encoder_compare.cjs), and the
frozen additional cases are [tools/encoder_compare_cases.json](tools/encoder_compare_cases.json).

Here, a **correct top choice** means encoding an instruction and each candidate
label with the frozen pretrained encoder, computing their cosine similarities,
and checking whether the highest-scoring label is the expected one. For example,
an instruction such as "buy this item" should rank "Add to cart" above unrelated
labels. There were **zero RL training steps and zero encoder fine-tuning steps**
in this experiment. The score is not browser task success or a trained policy's
accuracy. More policy training may improve decisions, but cannot recover text
distinctions discarded by its input representation; ordered tokens and exact
values should remain available alongside pooled vectors.

Run it with:

```sh
bash ocean/webnav/tools/encoder_compare.sh
```

The script bootstraps the existing Potion-8M assets and Rust tokenizer binding
with `make -f ocean/webnav/Makefile test-text` when they are absent, then
downloads the pinned comparison assets into ignored `build/webnav/encoder-comparison/`,
checks their SHA-256 values, and writes the machine-readable result to
`build/webnav/encoder-comparison/encoder-comparison.json`. The comparison used
Node 24.18.0, the Hugging Face Rust tokenizer Node binding 0.23.2, and
`onnxruntime-node` 1.22.0. Potion is a direct Node lookup/pooling harness over
the F32 safetensors; MiniLM is the quantized AVX2 ONNX model with one CPU
thread. No Python was used.

## Measured semantic ranking

The existing 24 cases are the unchanged [text diagnostic](tests/text_cases.json).
The broader set has 58 hand-authored contrastive cases written before this
run. They cover paraphrase, order, negation, numeric values, role/state,
distractors, and Unicode noise. The 58-case set is a transparent development
diagnostic, not an independent accuracy benchmark, representative benchmark,
or learned-policy score.

| Encoder | Existing 24 | Broader 58 | Combined 82 |
| --- | ---: | ---: | ---: |
| Potion-8M | 16/24 (66.7%) | 45/58 (77.6%) | 61/82 (74.4%) |
| Potion-32M | 19/24 (79.2%) | 45/58 (77.6%) | 64/82 (78.0%) |
| MiniLM-L6-v2 ONNX int8 AVX2 | 16/24 (66.7%) | 41/58 (70.7%) | 57/82 (69.5%) |

Those are strict top-1 scores. The JSON also reports `strict_unique_*` and
`expected_credit_*`. The latter gives a case `1/k` credit when the expected
candidate is among `k` candidates within `1e-6` of the maximum score. That
tie-aware number is the safer one for the static models because the pooled
vectors can produce exact or near-exact ties; it should be used when comparing
small score changes.

The tie-aware summary is shown below as `top-1 / strict-unique / expected
credit` (the expected-credit values are counts out of the group size):

| Encoder | Existing 24 | Broader 58 | Combined 82 |
| --- | ---: | ---: | ---: |
| Potion-8M | 16 / 14 / 16.0 | 45 / 43 / 45.0 | 61 / 57 / 61.0 |
| Potion-32M | 19 / 16 / 18.5 | 45 / 42 / 44.0 | 64 / 58 / 62.5 |
| MiniLM-L6-v2 ONNX int8 AVX2 | 16 / 16 / 16.0 | 41 / 41 / 41.0 | 57 / 57 / 57.0 |

The broader per-category results show where the representation fails:

| Category | Potion-8M | Potion-32M | MiniLM |
| --- | ---: | ---: | ---: |
| paraphrase (8) | 8/8 | 8/8 | 8/8 |
| order (8) | 6/8 | 7/8 | 5/8 |
| negation (8) | 1/8 | 1/8 | 1/8 |
| numeric (10) | 6/10 | 5/10 | 5/10 |
| role/state (10) | 10/10 | 10/10 | 8/10 |
| distractor (8) | 8/8 | 8/8 | 8/8 |
| Unicode/noise (6) | 6/6 | 6/6 | 6/6 |

Potion-32M improves the existing diagnostic by three cases, but does not
improve the broader set over Potion-8M. MiniLM does not win this cosine
ranking task despite being contextual. All three encoders fail almost the
same negation cases. This is evidence that an embedding nearest-neighbor
score is the wrong authority for polarity and numeric comparisons; it is not
evidence that MiniLM or Potion is generally poor at language.

The order result also needs careful interpretation. Static mean pooling is
permutation-invariant, so an exact reversal with the same token multiset must
produce the same pooled vector. A contextual model can represent order, but
the controller still has to use that representation correctly. Exact ordered
tokens and typed instruction features remain necessary.

## Measured CPU costs

These are one run of the Node research harness on the local machine. “Batch1
text” means one text encoded by one call at a time, including the tokenizer
and model/backend call. “Cached case scoring” uses already encoded query and
candidate vectors, then performs Map lookups and three dot products; it does
not include tokenization or model inference.

| Encoder | Model files | Load | Batch1 p50 | Batch1 p95 | Cached scoring p50 | Cached scoring p95 | Process RSS (cumulative) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Potion-8M | 30.9 MB | 0.52 s | 77.1 us | 4.59 ms | 1.92 us | 2.69 us | 139 MiB |
| Potion-32M | 130.7 MB | 0.77 s | 68.7 us | 1.39 ms | 3.45 us | 4.61 us | 383 MiB |
| MiniLM-L6-v2 int8 AVX2 | 23.5 MB | 0.83 s | 12.89 ms | 24.60 ms | 2.69 us | 3.65 us | 346 MiB |

The p95 values were observed without isolating Node/runtime, scheduler, or
shared-load effects; no profiling was performed to attribute the outliers. The
full result records p99 and mean as well. RSS is sampled after each model's
load and warmup while the three models run sequentially in one process. Each
row can therefore include allocations retained from earlier models and is
cumulative rather than an isolated per-model footprint. It includes the Node
process and its runtime, not just the model matrix. The MiniLM model file is
small because it is quantized, while its runtime still has a substantial
resident footprint. The backend and model artifacts are therefore not a
production memory forecast.

The full machine-readable record is
`build/webnav/encoder-comparison/encoder-comparison.json`; checksums are in
`build/webnav/encoder-comparison/SHA256SUMS`. The pinned revisions are:

* Potion-32M: `minishlab/potion-base-32M` revision
  `1e5a03f8eeb2c98b928fbbd846f22f816360919f`.
* MiniLM: `sentence-transformers/all-MiniLM-L6-v2` revision
  `1110a243fdf4706b3f48f1d95db1a4f5529b4d41`, using
  `onnx/model_quint8_avx2.onnx`.

The model cards and primary files are [Potion-32M](https://huggingface.co/minishlab/potion-base-32M),
[MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2),
and the [MiniLM ONNX file listing](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/tree/main/onnx).
The native deployment option is technically available because
[ONNX Runtime exposes C and C++ APIs](https://onnxruntime.ai/docs/api/c/c_cpp_api.html),
but this comparison intentionally uses Node onnxruntime as an isolated
reference. It does not add ONNX Runtime to PufferLib.

## What this says about the next design

Potion-32M is worth keeping as a larger static candidate because its top-1
score gains three of the existing 24 cases and its strict-unique score gains
two. The gain comes at about twice the median text cost and more than four
times the model memory. It is not worth promoting by itself: its broader
top-1 score is unchanged, its tie-aware broader score is slightly lower, and
its numeric and negation results do not improve.

MiniLM should remain a slow reference or offline teacher. Its fresh text
latency is roughly two orders of magnitude above Potion's median in this
harness. Whole-string caching makes repeated candidates cheap, but a browser
page with many new labels would pay the contextual cost whenever those labels
change.

The next production-facing semantic ablation should therefore retain the
current Potion vector and add deterministic features for:

* ordered token IDs or a bounded per-token Potion sequence;
* exact normalized names, values, placeholders, and instruction spans;
* signed numeric values, decimals, currencies, units, and comparison
  direction;
* polarity and quantifier markers such as `not`, `all`, `any`, and `none`;
* role, control state, geometry, parent/label relation, and occurrence identity.

The Bend environment can own the bounded feature contract and its laws. The
current native tokenizer/model provider remains a replaceable bridge, and
this report gives a fixed semantic and latency gate for changing it. The
static table should be enlarged only when it wins the WebNav-specific test
under the actual memory budget; a larger pooled table cannot recover order or
typed comparison structure that pooling discarded.
