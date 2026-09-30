# Catalog adapter validation — 2026-09-29

All 3 task adapters passed native regression tests, built with the native CUDA trainer, and ran on pinned original browser pages. This is a short baseline, not a claim that these tasks are solved. Stock CPU Bend owns environment behavior and generation.

Final checkpoint: `checkpoints/webnav_catalog/1790681457404/0000000000999424.bin` (999,424 steps, H64/L1). Greedy evaluation is separate from the sampled-action training dashboard.

| Task | Random native /100 | Learned native /20 | Learned browser /5 |
|---|---:|---:|---:|
| phone-book | 3 | 1 | 0 |
| order-food | 0 | 0 | 0 |
| search-engine | 2 | 0 | 0 |

All browser tasks also completed two random-action smoke episodes. Browser scores use original generated pages and real CDP input; no private goals enter the policy. Native and browser distributions differ. Five browser episodes per task are only a diagnostic sample. Exact results, partial rewards where supported, and source/checkpoint hashes are in [RESULTS.json](RESULTS.json).

Integration fixes: corrected C++ allocator casts for numeric/catalog, fixed the numeric reset-history test to allow newly observed public values, and retained the numeric transport requirement to clear a full field before insertion. Test/evaluation runners disable process crash dumps to avoid hanging in WSL’s crash collector after assertion failures.

Phone-book partial credit remains separate from full credit. Original duplicate contacts can be ambiguous; food quantities and search/page sequences require more training than the earlier click tasks. The action preset copies literal quoted search strings and exposes public quantity/ordinal features.

Next: address sparse-reward exploration and greedy timeout loops with task curricula and a more structured action representation; inspect failure traces, broaden Bend generator distributions, then run larger held-out evaluations. Longer training alone has not been established as a fix. Do not count a working action transport as a learned solution. Build/run commands and action limitations are in [README.md](README.md).
