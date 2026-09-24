# Text encoding investigation

Decision so far: **do not block MiniWoB ports on a fancy encoder**. Define a stable text/node contract and compare frozen features alongside the ports. The first promising low-cost semantic candidate is Model2Vec/Potion; it closely matches the proposed precomputed lookup table. It is not yet integrated into the policy.

A tokenizer maps text to discrete IDs. Those IDs are labels, not semantic coordinates; their numeric distances mean nothing. A frozen static embedding model maps each ID to a pretrained vector and pools the vectors. A contextual model computes representations dependent on surrounding tokens. Caching contextual results requires keys for whole input strings/context, not just individual tokens. Static embeddings can compose an unseen string from known subwords without a neural forward pass, although their pooled representation loses word order and is weak on logic and numbers.

[Model2Vec](https://github.com/MinishLab/model2vec) and [Potion 8M](https://huggingface.co/minishlab/potion-base-8M) provide a ready-made frozen solution. We inspected and pinned revision `bf8b056651a2c21b8d2565580b8569da283cab23`: 29,528 WordPiece vocabulary entries, 256 float32 dimensions, 30,236,760 bytes in the weights file (~28.84 MiB), with normalized pooled output. The model card lists MIT. Use the matching tokenizer and exact preprocessing; tokenizer IDs from a different vocabulary would index the wrong vectors. Model/source licenses and source-site content permissions are separate concerns; this is not blanket permission to redistribute captured sites.

The original [World of Bits paper](https://proceedings.mlr.press/v70/shi17a/shi17a.pdf) used CNN image features plus query-to-DOM text matching, not a modern language model. It trained policies with demonstrations and RL. The later [MiniWoB++ paper](https://nlp.stanford.edu/pubs/liu2018reinforcement.pdf) introduced DOMNET with text, spatial/tree structure and query matching. These support starting with cheap lexical features and shared element scoring. Neither establishes that a tokenizer alone supplies semantic understanding.

Native C development probe

```sh
bash ocean/webnav/tools/text_probe.sh
```

The script downloads checksum-pinned model/tokenizer files into ignored build/webnav/reference and compiles a small C executable. It uses no Python and trains nothing. The probe implements an explicitly limited ASCII WordPiece subset, looks up frozen vectors, sums and L2-normalizes them (equivalent to normalized mean for a nonempty sequence), and compares query/candidate cosine similarity. It rejects Unicode and special-token spelling inputs. It must pass upstream tokenizer/vector parity tests before production use.

One warm run on the local i5-7400, one CPU thread, Clang 19 -O3:

| Method | Microseconds per short string | Diagnostic nearest-label matches |
| --- | ---: | ---: |
| Hashed token counts, 256 dimensions | 1.265 | 14 / 24 |
| Frozen Potion vectors, 256 dimensions | 1.405 | 16 / 24 |
| Materialize already-cached vector | 0.141 | Not evaluated |

All dimensions are consumed through volatile writes to prevent the compiler discarding most output work. Both encode times include ASCII tokenization, pooling, normalization and that output consumption. Cached materialization excludes key hashing, lookup and eviction. The corpus contains only 24 short hand-authored diagnostic queries, repeated 100,000 times: this is cache-friendly kernel timing, not a prediction for diverse pages, cold model loading, extension overhead, or end-to-end RL throughput. Exact results, model hashes and limits are in [TEXT_PROBE_RESULTS.json](TEXT_PROBE_RESULTS.json).

Useful matches included “where is my parcel” -> “Track shipment” and “buy this item” -> “Add to cart.” Misses included “do not delete the message” -> “Delete the message,” selecting a price below 20, sorting cheapest-first, and distinguishing translation direction. “English to French” and “French to English” have the same bag of tokens: pooled vectors cannot retain that distinction in principle. This diagnostic is intentionally tiny and contains adverse cases, not a statistically representative benchmark.

Consequently, use semantic embeddings as one channel alongside original token order, exact name/ID matching, numeric values/comparison features, role, control state and DOM relationships. Let the policy decide actions. The nearest cosine label alone is not the intended controller. Keep occurrence-specific node state outside a cached text vector. Hash caches by model/tokenizer/normalizer/pooling version plus exact text, and do not merge identifiers or numbers through careless normalization.

Memory matters even if encoding is cheap. For example, storing 128 nodes x 256 float32 features is 128 KiB per observation; 1,024 agents x 32 rollout steps would be 4 GiB just for those node vectors. That exceeds the current GPU's memory before the policy and optimizer. Avoid blindly expanding the existing float rollout buffer: keep compact token/text IDs with immutable shared caches, gather when needed, and investigate lower precision or smaller projections with measured accuracy. One million distinct 256-dimensional float32 text vectors alone occupy about 0.95 GiB before cache metadata. Streaming live inference and retaining an RL rollout have different memory budgets.

Next experiment: certify preprocessing against the upstream tokenizer, freeze a larger realistic corpus with exact-match/paraphrase/numeric/order splits, and compare lexical features, Potion/static lookup, and [MiniLM-L6](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2) as a contextual reference (384-dimensional output; model card lists Apache-2.0). Measure fresh text, Unicode, latency distributions, node-count scaling, cache hit/miss behavior, peak memory and downstream task success. Do not assume the published Model2Vec speedup transfers to this machine or workload. A browser extension deployment is a separate measurement; this probe is native Linux C.

Validated native follow-up

A separate reusable implementation now lives in `text_encoder.c/.h`; the original ASCII probe above is retained as an early measurement, not the production path. It uses ICU for the pinned BertNormalizer profile and WordPiece, handles explicit byte lengths and Unicode, preserves special-token behavior, reports unknown/truncated tokens, rejects invalid UTF-8, and bounds input to 32 KiB / 512 tokens. It locks the exact tokenizer and weights profile. `text_cache.c/.h` adds immutable exact-string caches; a missing string can be encoded live using the same model. Cache construction rejects truncated strings rather than silently recording an incomplete embedding.

Run `make -f ocean/webnav/Makefile test-text`. Reference generation uses pinned Hugging Face Rust tokenizer Node bindings (0.23.2), without Python. On 1,120 fixed/adversarial/generated test strings, token IDs match exactly; output vectors match an independent JavaScript normalized-pooling calculation. AddressSanitizer and UndefinedBehaviorSanitizer pass. These are compatibility tests, not a formal proof for every Unicode string or direct execution of the Python Model2Vec package. The explicit 512-token limit is our profile; longer full-model inputs would differ.

Initial corpus measurement: 268 distinct strings extracted from Books to Scrape, Adafruit, Allbirds and Project Gutenberg initial pages. One warm repeated run measured encoding p50 1.60 us / p95 7.20 us; cache-hit p50 0.20 us / p95 0.30 us, including key lookup and consuming all output dimensions. A synthetic 128-node workload measured about 628 us for all-live encoding, 20 us for all cache hits, and 104 us when roughly 10% were re-encoded. These are small-corpus CPU kernel measurements; they exclude browser extraction and policy/training, and are not extension or diverse-web throughput guarantees. Peak process RSS was about 38 MiB in that run. See ITERATION_RESULTS.json for precise values, hashes, load times and limitations.

The static encoder is now a credible implementation candidate, but it is not integrated into the RL policy. The hand-authored semantic diagnostic remains only 16/24 nearest-label matches, and no new accuracy claim follows from passing tokenizer parity. Exact/order/numeric features and a broader comparison are still required.

Follow-up research: [ENCODER_RESEARCH.md](ENCODER_RESEARCH.md) compares Potion sizes, ordered token features, quantization and a contextual reference. Larger pooled tables do not recover discarded token order; semantic benchmark/policy ablations remain required before choosing an encoder.

Measured follow-up: [ENCODER_COMPARISON.md](ENCODER_COMPARISON.md) now compares 8M, 32M and quantized MiniLM on 82 development diagnostics. Tie-aware totals are 61, 62.5 and 57 respectively. The Node/ONNX research timings are not native C latency estimates. No production encoder replacement or learned-policy ablation has been made.
