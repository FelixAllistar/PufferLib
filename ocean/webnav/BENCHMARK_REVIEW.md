# Benchmark rerun, 2026-09-20

The existing CPU Bend generation/transition benchmarks and native text/cache benchmarks were run, together with all MiniWoB, DOM and text correctness checks. This run does not compare Potion-32M, MiniLM, or a trained policy with ordered-token features.

Hardware: Intel i5-7400, four CPU cores. Each benchmark uses one thread. Other compiler jobs were observed on the shared machine, so these are development measurements under variable load, not an isolated hardware comparison. Values below are medians of three runs; form generation and active-click phases each last three seconds per run, with runtime warmup excluded.

| Measurement | Median | Range across runs |
| --- | ---: | ---: |
| Bend form generation + transport | 535,102 forms/sec | 468,253–555,750 |
| Bend active widget clicks + transport | 1,225,904 clicks/sec | 1,139,038–1,310,768 |
| Website corpus live encoding, mean/string | 2.85 µs | 2.44–3.22 µs |
| Website corpus cached lookup/materialization, mean/string | 0.165 µs | 0.155–0.174 µs |
| Generated-form text live encoding, mean/string | 3.00 µs | 2.90–3.39 µs |
| Generated-form text cached lookup/materialization, mean/string | 0.494 µs | 0.411–0.532 µs |

The website corpus contains 268 distinct strings from four initial-page captures. The generated corpus contains 5,801 distinct strings from 1,000 radio and 1,000 checkbox forms, seeds 42..1041. Both are repeatedly accessed warm corpora after cache construction. Peak text benchmark RSS was about 38 MiB / 46 MiB respectively. Neither corpus is a broad semantic evaluation.

For 128 text items (one string per nominal node), website-corpus processing took a median 284 µs when all were encoded, 17.2 µs when all were cached, and 48.0 µs with approximately 10% re-encoding. Generated-form text took 318 / 25.9 / 71.4 µs respectively. These are synthetic text workloads, not end-to-end page observation, browser-extension or policy-inference timings. The active-click benchmark deliberately avoids terminal submissions and holds logical time fixed; it is not episode or RL throughput.

Correctness checks passed: 22 Bend laws; 5,456 checkbox state/goal cases; 8,192 generated forms; 3,448 radio combinations; seven browser modes totaling 7,000 episodes / 12,901 actions; DOM extraction checks; 1,120 reference tokenizer/vector cases; and cache integrity/profile checks. Browser counts are conformance coverage, not learned-policy success.

The native Unicode encoder scored 16/24 on the existing small semantic diagnostic, reproducing the older probe's result. Reversing “translate from English to French” to “translate from French to English” changed ordered token IDs but yielded pooled vectors with cosine 0.99999994 and maximum component difference 1.49e-8. The tiny floating-point difference is summation noise; pooling has discarded useful order. This demonstrates a representation limitation, not the accuracy of a future policy using ordered features.

Assessment: the simulator speed is promising. Preserve the current 8M table as a baseline while retaining ordered tokens and exact strings/numbers in policy features. Encode only changed text and avoid copying all vectors into every rollout record. Random task labels also require attention to cache growth; an unbounded table of all possible generated strings is not a practical cache design. Larger/contextual models still need their own measured comparison before choosing a replacement.

Evidence is under `build/webnav/run-review/`: `summary.json`, `checks.log`, three `forms-*.json`, three `site-text-*.json`, three `generated-text-*.json`, `semantic.json`, and both public form JSONL exports. The semantic check's C source and executable are retained there as well. No policy was trained and no environment semantics changed for this rerun.

Reproduce the existing targets from the repository root:

```sh
make -f ocean/webnav/Makefile test-miniwob test-dom test-text
make -f ocean/webnav/Makefile bench-forms bench-text
build/webnav/generate_forms click-option 1000 42 > build/webnav/radio-forms.jsonl
```
