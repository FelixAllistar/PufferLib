# Reset-bank refresh — 2026-09-13

## Completed first build and requested expansion

The first build completed in 2,321 seconds (38m41s), with all final audits
passing: **78,864 training states**, 8,868 seed-held-out states, and 3,120
latest-date states. The 829 parity-incompatible episodes were discarded.
Training states cover 6,572 games and 27 dates; the newest date is held out.

The user subsequently requested doubling the training bank. The incremental
expansion targets **at least 157,728 training states**, using all 10,551
previously unprocessed archive members rather than duplicating snapshots.
`run_double_reset_bank.sh` skips the first 300 hash-ordered episodes/day,
reuses the existing audited shards read-only, and writes
`state_bank/diverse_20260913_double/`. No additional downloads are required.
The expected size is approximately 175k–180k training states, subject to parity
rejections. This is an estimate, not a finished count.

The expansion runs in tmux `reset-double-20260913`; its live log is
`logs/reset_double_20260913/build.log`. It automatically runs the final audit
after building. The audit requires the minimum count and byte-identical
preservation of every previous full/holdout/future snapshot. Existing future
holdout seeds cannot enter training through newly processed games. No training
config is switched automatically. The original completed bank remains usable.

Scope: improve **offline stateset construction**, expand replay coverage, and
provide full and scenario banks. Do not change the running sweep, its config,
the training executable, reset runtime, or rewards. Runtime speed was not the
user's bottleneck.

## What OmniReset does differently

[OmniReset: Emergent Dexterity via Diverse Resets and Large-Scale Reinforcement
Learning](https://arxiv.org/html/2603.15789v1) generates feasible reaching,
near-object, grasped, and near-goal states offline, rejects invalid samples,
and trains from their union without a staged curriculum. These states cover
interaction possibilities rather than only demonstrated trajectories. Its
experiments show that broader reset diversity and larger parallel PPO batches
help connect near-goal competence to full-task success. It still uses a shaped
reward; it is not a reward-free method. Robotics-specific actor/critic and
exploration choices are additional differences, not changes made here.

| Aspect | Previous Kaggriculture setup | This refresh |
| --- | --- | --- |
| Data coverage | Full bank: 21,557 states, 4,120 episodes, August 16–21 only | Target: 300 episodes/date across August 16–September 12 |
| Time coverage | 8,686 states (40.3%) in turns 600–719 | Eight uniformly spaced time bands, one random anchor in each |
| Extra scenarios | Tag-based sampling; many related snapshots | Up to four additional market, maintenance, investment, recovery anchors/episode |
| Validation | Every frame and restored next step checked | Same guarantees, whole episode discarded on any mismatch |
| Holdouts | No explicit seed/date holdout in that build | Stable 10% seed-group holdout; September 12 reserved, excluding previously seen seeds |
| Build work | Full indexing plus reconstruction; separate stage builds could repeat reconstruction | One episode decode/reconstruction; resumable daily shards; cheap verified copies for subsets |
| Memory/disk | Large intermediate indexes/manifests and repeated builds | No raw extraction; bounded worker memory; compressed archives and reusable binary shards |

Source code examined: `index_replay_states.py`, `build_replay_state_bank.py`,
`select_replay_state_stage.py`, `slice_replay_state_bank.py`,
`run_fullbank_curriculum.sh`, and the native reset/curriculum code in
`kaggriculture.h` / `kaggriculture.cu`.

The previous procedural curriculum is a different path: short constructed
lessons, manipulated inventory/prices, success gates, and a passive opponent
for lessons. Enabling it bypasses replay-bank resets. Scenario banks here are
ordinary complete replay states; they do not enable those lesson mechanics.
This distinction is a plausible reason full-bank training transferred better,
not a demonstrated causal result.

Other remaining overfitting risks: all replay states still come from existing
bots; snapshots restore simulator RNG state; and the actor can observe the
reset-source flag. More states do not by themselves eliminate these risks.
No random state perturbations, reseeding, or observation changes were made.
Some inherited tag names are misleading: `maintenance_profitable` means the
recorded bot performed maintenance, not that a counterfactual proved profit.
The new maintenance subset combines both maintenance labels.

## Data and build

Remote repository: `/workspace/PufferLib`.

Downloaded all 11 September 2–12 archives: **7,280 episodes, 6,327,980,187 bytes
compressed (5.89 GiB)**. They remain in `/workspace/elite_replays/raw`.
The downloader limits new compressed downloads to 9 GiB and preserves 35 GiB
free space. The build projects shard plus merged-bank storage and preserves
30 GiB. Existing archives/banks are not removed.

Build output: `ocean/kaggriculture/state_bank/diverse_20260913/`.

- `full.kgb`: training split, all selected stages mixed together.
- `market.kgb`, `maintenance.kgb`, `investment.kgb`, `recovery.kgb`: small
  scenario subsets copied from training states, with checksums verified.
- `holdout.kgb`: held-out seed groups; never put this in training.
- `future.kgb`: latest-date holdout, with previously seen seeds excluded.
- `shards/YYYY-MM-DD/`: reusable per-day banks; retain for incremental builds.
- `summary.json`: completion marker, actual counts and coverage distributions.
- `audit.json`: post-build CPU format/checksum/native-deserialization/config
  and holdout-leakage validation. Only use the banks after this says passed.

The six-worker build runs at nice level 10 in tmux session
`reset-build-20260913`. It does not use the GPU. Logs and the download report
are in `logs/reset_refresh_20260913/`. No bank is automatically activated.
Do not use `.part` files or the tiny pilot banks for training.

Initial two-episode benchmark: new end-to-end pilot **3.33 seconds**, old
reconstruction alone **5.61 seconds**, all **24 snapshot hashes identical**.
Both checked 1,440 official frames and all restored next steps. This is a
small same-machine comparison, not a statistically controlled benchmark.
Additional benefits are six-way parallelism and avoiding repeated replay
reconstruction for each scenario bank. Full-run timing belongs in the final
build log; do not extrapolate the small pilot as a guaranteed speedup.

The new script optionally uses `orjson`, installed only in
`ocean/kaggriculture/_reset_deps` on Vast; the main Python environment was not
changed. Standard-library JSON remains supported. The faster equality check
retains the old checker's strict type semantics, including bool/int and
int/float distinctions.

## Reuse and evaluation

Add new archive paths and rerun `build_diverse_reset_bank.py` with the same
output and settings to reuse completed daily shards. A changed sample count,
holdout boundary, or native library hash is rejected rather than silently
mixing incompatible shards; use a new output directory for such changes.

After the sweep, a sensible experiment is full-bank mixed resets with ordinary
root starts, compared with root-only training, using matched training budgets
and a fixed common opponent pool. Score root starts separately from held-out
reset starts. Keep holdout scores out of training/selection until the final
comparison. This is an experiment proposal, not a launched run or proven
solution to overfitting.
