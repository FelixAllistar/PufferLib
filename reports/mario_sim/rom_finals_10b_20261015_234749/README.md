# Final 10B checkpoint check-in

Both finished trials were downloaded with SHA-256 verification. Frozen CPU ROM evaluations use the same seed, evaluator binary, physical configuration, episode limits and sampled actions as the historical 2.15B and 6.44B comparisons. The continuing GPU sweep was retained; remote evaluation initially used CPU cores 46 and 47, then cores 45–47 after measuring available CPU quota.

Completed actual-ROM scores, including all 32 per-level comparisons: [comparison/ROM_RESULTS.md](comparison/ROM_RESULTS.md). The strict simulator/ROM report, which retains the failed 5-2 parity panel, is [comparison/README.md](comparison/README.md). Each final model has 1,216 test episodes: 32 continuing games, 32 fresh 1-1 tests, two FPG panels of 64 attempts, and 32 attempts on each of 32 levels. The duplicate 1-1 panels check reproducibility.

`analysis.json` contains totals, water-level results, zero-clear levels, and training score decomposition. `download_manifest.json` records checkpoint provenance and hashes. `results/` retains raw trajectories, cases, logs and commands; `comparison/summary.json` links the two historical source reports. Invalid parity panels remain explicitly invalid.

The training score uses FPG curriculum depth rather than current success rate, so retained curriculum progress can hide skill loss. Training starts uniformly at the 32 natural level entrances; extra FPG curriculum starts cover only 1-1. Encounter-specific reset states are a candidate experiment, not a demonstrated fix.

A simulator CPU-register mismatch was detected in run 0, 5-2, episode 30 at frame 2,010; RAM matched at first divergence. Its two unfinished attempts were completed directly in the ROM, with a repeated paired-run control and matching pre-divergence trajectory. This completes actual-ROM scoring without claiming full simulator parity. Run 0: 331/1,024 per-level clears; run 1: 436/1,024.
