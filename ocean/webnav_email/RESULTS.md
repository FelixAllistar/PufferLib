# Email adapter validation — 2026-09-29

All 10 task adapters passed native regression tests, built with the native CUDA trainer, and ran on pinned original browser pages. This is a short baseline, not a claim that these tasks are solved. Stock CPU Bend owns environment behavior and generation.

Final checkpoint: `checkpoints/webnav_email/1790681591815/0000000000999424.bin` (999,424 steps, H64/L1). Greedy evaluation is separate from the sampled-action training dashboard.

| Task | Random native /100 | Learned native /20 | Learned browser /5 |
|---|---:|---:|---:|
| email-inbox-delete | 31 | 0 | 0 |
| email-inbox-forward-nl-turk | 0 | 0 | 0 |
| email-inbox-forward-nl | 0 | 0 | 0 |
| email-inbox-forward | 0 | 0 | 0 |
| email-inbox-important | 30 | 0 | 0 |
| email-inbox-nl-turk | 3 | 0 | 0 |
| email-inbox-noscroll | 9 | 0 | 0 |
| email-inbox-reply | 0 | 0 | 0 |
| email-inbox-star-reply | 16 | 0 | 0 |
| email-inbox | 8 | 0 | 0 |

All browser tasks also completed two random-action smoke episodes. Browser scores use original generated pages and real CDP input; no private goals enter the policy. Native and browser distributions differ. Five browser episodes per task are only a diagnostic sample. Exact results, partial rewards where supported, and source/checkpoint hashes are in [RESULTS.json](RESULTS.json).

Integration fixes: corrected C++ allocator casts for numeric/catalog, fixed the numeric reset-history test to allow newly observed public values, and retained the numeric transport requirement to clear a full field before insertion. Test/evaluation runners disable process crash dumps to avoid hanging in WSL’s crash collector after assertion failures.

Root requested and integrated the initially missing public-only original-browser evaluator. Reply/forward involve long sparse-reward sequences and public text-copy choices; original NL/Turk instruction templates are broader than native generation. Passing this adapter does not establish semantic comprehension or reliable typing.

Next: address sparse-reward exploration and greedy timeout loops with task curricula and a more structured action representation; inspect failure traces, broaden Bend generator distributions, then run larger held-out evaluations. Longer training alone has not been established as a fix. Do not count a working action transport as a learned solution. Build/run commands and action limitations are in [README.md](README.md).
