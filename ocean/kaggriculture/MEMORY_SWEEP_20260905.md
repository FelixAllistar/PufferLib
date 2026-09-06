# Macro PPO memory-aware sweep

One active training config remains `config/kaggriculture.ini`. The driver saves
an immutable JSON audit of its resolved settings, not another launch config.
All training and evaluation use `./puffer`; Python only orchestrates subprocesses.

Bootstrap: PSRO-selected autofarm 999.29M, 256x3, macro2 with automatic operation.
Freeze the champion (and EMA sidecar), rewards, and a copied eight-policy league.
No automatic league promotions or reward changes. Rolling snapshots are disabled
for comparable trials; configured live self-play and native bots remain enabled.

Initial phase: 4096x256, 8192x128, 4096x128, 8192x64 (agents x horizon), each with
minibatches 2048/1024/4096 and LR .004. Also test 16384x32 and 16384x64 with
minibatch 2048. Fourteen 300M-step trials (updated before queue started).
Refinement and second-seed trials also use 300M steps. The largest rollout
is 1,048,576 transitions, matching the successful 4096x256 run. This is a heuristic
memory bound, not a VRAM guarantee: environment and recurrent allocations also vary.
Each trial first executes two native rollout epochs to check allocations/updates.
Only recognizable OOM failures are skipped after a device-health query; other
failures stop for inspection. Driver/device faults are not silently retried.

Next: select two configs by final-checkpoint mean score then cash margin against
the same uniform opponent panel (not dashboard money). Test LR .002/.006 and
neighboring horizons within the rollout budget, avoiding already-tested configs.
Repeat two finalists with seed 43. Initial seed is 42. Every trial starts from
the same champion, not the preceding trial. Save/evaluate roughly every 25M steps;
final checkpoints drive automatic selection to limit peak-selection bias. Evaluate
the original champion on the same panel as baseline. Results include all checkpoint
scores, money, margins, raw matches, commands, logs, and training elapsed seconds.
64 evaluation games per opponent; deterministic masked actions and fixed eval seed.
This is a screening protocol, not proof of statistically significant superiority.

More agents means more parallel streams, not necessarily independent complete
episodes: horizons truncate sequences and live self-play shares policies. Compare
4096x128 vs 8192x128 for agent count at the same horizon; 4096x256 vs 8192x128 holds
rollout size fixed but changes recurrent sequence length. Do not force 8192x256
to fit by silently changing another parameter.

Use `sweep_macro_memory.py --prepare --output ... --champion ... --league ...`
to snapshot settings before waiting for the current job. Then `--run --output ...`.
Completed trial JSON records are resumable. Interrupted trial checkpoints cause a
stop rather than being overwritten. A fresh output ID is required for a fresh sweep.
No training launch config is changed. Do not run two copies on the same output/GPU.
