# Numeric adapter validation — 2026-09-29

All 9 task adapters passed native regression tests, built with the native CUDA trainer, and ran on pinned original browser pages. This is a short baseline, not a claim that these tasks are solved. Stock CPU Bend owns environment behavior and generation.

Final checkpoint: `checkpoints/webnav_numeric/1790681756242/0000000000999424.bin` (999,424 steps, H64/L1). Greedy evaluation is separate from the sampled-action training dashboard.

| Task | Random native /100 | Learned native /20 | Learned browser /5 |
|---|---:|---:|---:|
| ascending-numbers | 2 | 20 | 5 |
| find-greatest | 26 | 15 | 4 |
| generate-number | 29 | 8 | 2 |
| guess-number | 3 | 0 | 0 |
| hot-cold | 0 | 0 | 0 |
| number-checkboxes | 0 | 0 | 0 |
| odd-or-even | 3 | 1 | 0 |
| simple-algebra | 0 | 0 | 0 |
| simple-arithmetic | 0 | 0 | 0 |

All browser tasks also completed two random-action smoke episodes. Browser scores use original generated pages and real CDP input; no private goals enter the policy. Native and browser distributions differ. Five browser episodes per task are only a diagnostic sample. Exact results, partial rewards where supported, and source/checkpoint hashes are in [RESULTS.json](RESULTS.json).

Integration fixes: corrected C++ allocator casts for numeric/catalog, fixed the numeric reset-history test to allow newly observed public values, and retained the numeric transport requirement to clear a full field before insertion. Test/evaluation runners disable process crash dumps to avoid hanging in WSL’s crash collector after assertion failures.

The initial pilot exposed oversized numeric feedback values. Final features clip parsed public numeric values to [-1000,1000] before dividing by 100; regression tests include overflowing decimal strings. The final checkpoint was trained fresh after this correction. The existing Bend insertion transport requires select-all then backspace before replacing a full field; masks preserve that restriction. Glyph pixels remain absent, and hot/cold coordinate exploration is expensive.

Next: address sparse-reward exploration and greedy timeout loops with task curricula and a more structured action representation; inspect failure traces, broaden Bend generator distributions, then run larger held-out evaluations. Longer training alone has not been established as a fix. Do not count a working action transport as a learned solution. Build/run commands and action limitations are in [README.md](README.md).
