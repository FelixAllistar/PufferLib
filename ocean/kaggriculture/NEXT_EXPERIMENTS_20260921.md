# Post-sweep multi-intent / BC qualification

The user ended the old-controller sweep and subsequently authorized replacing
the active install, with work visible in their tmux. **The active remote install
is now `/workspace/PufferLib`, mode 2 / executor 2, H512/L3, policy ABI 5.**
Both `./puffer` and `./kaggriculture` were rebuilt there for the RTX 5060 Ti.
The standalone BC trainer is `ocean/kaggriculture/build/kag_bc` (not a
`./puffer bc` subcommand). Watch tmux session `ssh_tmux`, window `kag-bc`
(index 3); data preparation is visible in `kag-data` (index 4).

The exact old runtime/config/source is recoverable from
`/workspace/PufferLib-backups/pre-multi-intent.vh45lF/old-runtime.tar.gz`.
Old checkpoints, logs, remote-only files and the reset-state bank remain in
place. Old policy-v3 checkpoints cannot be loaded into the new policy-v5 model.
`/workspace/PufferLib-multi-intent` is retained staging, not the user's launch
directory. There is no new sweep or long PPO experiment running automatically.

Production build, active config check and 64-game BC fingerprint preflight
passed. Both two-update PPO checks (256 and 2,048 agents) also passed, loading
the qualified BC checkpoint with the full real reset bank and saving new PPO
checkpoints. The paired 100-epoch actor/joint BC pilots completed via
`deploy_20260921/gpu_checks_and_bc.sh`. Current exclusive output directory is
`qualification/production_20260921/pilot.5SAQq3`.

## Pilot outcome: integration passes; throughput regresses; downstream BC benefit untested

User clarification after these tests: previous BC became useful only after
eMAG + PPO fine-tuning, and then outperformed a raw start. Therefore standalone
BC playing strength is a diagnostic, **not** the pass/fail criterion for a
warm start. Retain both checkpoints and judge the intended pipeline against
fresh PPO under matched budgets/seeds. No downstream eMAG/PPO comparison was
run in this pilot, so no conclusion about BC's warm-start benefit is supported.

Both BC variants completed 100 epochs at LR 0.00005, batch 1, seed 7.
Held-out supervised CE fell from about 14.96 to 8.991, but labeled-head
accuracy is only 63.71% and exact labeled-row accuracy 3.55%. In reset-free,
deterministic evaluation against the fixed rules-bot mix, **both variants
finished with zero money and zero production across 64 games per seat**.
They spend the initial cash on seeds, repeatedly plant, never hire or sell,
and do not become working farms. These are poor standalone clones; they may
still provide useful representations for subsequent eMAG/PPO. No pilot
checkpoint was selected as the default load path or deleted.

Joint value coefficient 0.1 barely changes this outcome: held-out value RMSE
153.184 -> 152.736 and explained variance -0.0000688. It has not learned a
useful state-dependent value estimate in this pilot. Do not claim critic
pretraining has helped. The normalized value-loss scale and SGD update size,
weak opening/rare investment learning, and the BC-to-closed-loop distribution
shift are investigation candidates, not established diagnoses.

All six saved PPO/BC checkpoint arrays are finite and have the expected
4,025,560 float parameters and matching H512/L3, controller 2/2, policy-v5
sidecars; `summary.json` records the verification and metrics. `PASS.txt`
means the bounded integration workflow completed, **not** playing-strength
approval. The GPU was left idle; no long PPO job, sweep, DAgger job or further
hyperparameter sweep is queued. Diagnose rollout throughput before spending
a long-run budget, then compare BC -> eMAG/PPO with fresh PPO rather than
requiring pure BC to be a strong player. The install remains the user-requested new 2/2,
with the exact old install safely archived for a deliberate rollback.

## Frozen comparison

Use `bc_2_2_baseline_20260921.ini`, matched against the effective
`sweep_1789940990348_0000.start.ini`, not float-rounded TSV suggestions.
Rewards, gamma and H512/L3 match run 0. Daily-alive reward is **0**, not the
older pilot's 0.375302434. The deliberate differences are executor 2 and
`land_buy_min_days=0` (the user requested removing that restriction).
Thus this is a controller-bundle comparison, not an isolated estimate of
executor changes with identical land restrictions.

Keep reset-state training in the PPO baseline at probability 0.8. Use
reset-free evaluation with fixed seeds/opponents to compare actual complete
games. Annealing reset probability and reverting rewards are later, separate
ablations; neither is assumed beneficial without measurement.

## Current data

Local directory: `/home/felix/puffertank/elite_replays/bc_multi_2026-09-21/`.
Remote directory: `/workspace/PufferLib/data/`.
Initial qualification file: **`entity_2_2_policy5_baseline_v2.bc`**, with JSON and raw-intent
sidecars. The intermediate v1 used TSV-rounded crop/land growth coefficients;
v2 uses the exact effective startup values of 1. Keep v1 for provenance, but
use v2 for subsequent runs. Source fingerprint remains `7b7c37cbb92d935e`.
Semantics fingerprint: `538a2ad2f74b2658`. Dataset SHA-256:
`244139bb5888c88516a8cbe41fed24af07abe315f9453778d38731f7ef8a7e31`.

That qualification file retains the same 16 Majkel1337 games, 13/3 episode split, original action
tapes and actor labels. Only configured reward-derived returns change from
the September 20 pilot. Current coverage is still 99.30% of useful strategic
operation/crop/region signatures and 96.6% fully labeled nonempty market
queues. These are single-state diagnostics, NOT whole-game equivalence,
exact routing, arbitrary-teacher coverage or learned-policy performance.

The active BC config now uses **`entity_2_2_policy5_baseline_64g_v1.bc`**:
57 train / 7 holdout games, 40,829 / 5,015 actor-labeled rows. All 64 native
replays passed frame-by-frame official parity; zero episodes were rejected.
The 16 previously parsed games were reused, and 48 additional games were
parsed from the same September 17–19 local archives (no new download).
Stable sampling and episode splitting are unchanged. All primitive tapes
are available remotely in `data/majkel64_tapes`, with a portable provenance
manifest in `deploy_20260921/majkel64_manifest.json`. The source fingerprint
is still `7b7c37cbb92d935e`, and the reward/controller profile is unchanged.

## Qualification

Build for the remote RTX 5060 Ti (`NVCC_ARCH=sm_120`) into
`ocean/kaggriculture/build/qualification_20260921`.

- Local entity-BC tests: 13 passed, including native bridge and compiled
  preflight tests against v2. Multi-BC tests: 12 passed, including the original
  policy-v3 2/1 cross-build regression. Native multi-executor test passed.
- GPU actor-only and joint-value smokes passed on the current v2: two epochs
  each, 2.139 / 2.107 seconds including startup. Held-out action loss
  fell; every gradient inspected by the trainer and all checkpoint weights
  were finite. This is not evidence of good playing strength or critic
  calibration. Save/load with zero training preserves checkpoint bytes.
- Real GPU sampler/PPO-loss test passed sampled and greedy policies for seven
  controllers, now including 2/2. Removed old hard-coded market-mask stride
  from that test. CPU/GPU masks/actions match; stored-prefix likelihood ratios
  are 1 and visited-only gradients are finite.
- Full CPU/GPU environment parity passed: 16 cases x 1,440 turns, including
  new 2/2 fresh-start and scripted-reset cases. State and masks match exactly;
  floating observations/rewards/logs are compared with explicit tolerances.
  This is not yet a production PPO test using the saved-state bank.
- Entity-policy forward and gradient checks passed: CPU/GPU maximum forward
  error 7.45e-8; 54 finite differences across 18 matrices, maximum error
  4.77e-6. A gradient update reduced the test objective.
- The complete qualification script passed, including checkpoint roundtrip.
  Remote evidence: `qualification/baseline_v2_checks/PASS.txt` and sibling logs.

Run the bounded, exclusive-output qualification after all four binaries exist:

```bash
cd /workspace/PufferLib-multi-intent
bash ocean/kaggriculture/qualify_multi_intent.sh \
  data/entity_2_2_policy5_baseline_v2.bc \
  ocean/kaggriculture/build/qualification_20260921 \
  qualification/baseline_v2_checks
```

The script refuses an existing output directory or a running `puffer` process.
It snapshots its profile/hashes and performs preflight, CPU/GPU environment
parity (including 2/2 fresh/scripted-reset starts), encoder gradients, PPO sampling,
two-epoch actor/joint smokes and checkpoint roundtrip. Its checkpoints are
test artifacts, not candidates for promotion. Full GPU PPO startup, memory,
throughput, closed-loop scores and export are separate gates.

## Next experiments, in order

1. The full native executable is built and installed. The small 256-agent
   PPO/BC-checkpoint-load smoke passed two updates with the real 78,864-state
   reset bank, eight frozen banks and reset probability 0.8. Both
   2,048-agent updates passed too, at about 15.4 GiB VRAM. **Throughput gate is
   not satisfactory yet:** production-size updates took 158.67 / 158.08 s
   (9,293 / 9,328 steps/s). The second update splits into 44.29 s environment,
   105.55 s model/sampler and 8.20 s training. Model timing includes prefix-dependent action masking;
   it does not isolate encoder neural-network cost. The old run-0 metric log
   (`logs/kaggriculture/sweep_1789940990348_0000.ini`) provides a more useful
   comparison: its second update took 26.85 s, with 2.92 s model/sampler,
   17.20 s environment and 6.72 s training, at the same 2,048-agent geometry.
   These are not identical policy/state trajectories, but the timing points
   primarily to rollout execution rather than PPO optimization. The new
   legality-mask preview calls the multi-intent worker planner, which also
   runs during action execution; repeated planning and much larger per-thread
   scratch space are suspects, not yet isolated causes. Profile
   rollout/sampler/executor work before starting a 300M-step job. Static CUDA
   resource inspection also shows sampler stack 4,240 -> 24,560 bytes/thread
   and environment-step stack 15,488 -> 38,240 bytes/thread compared with the
   archived old binary. This supports a scratch-space/occupancy investigation,
   but is not by itself a measured causal attribution. Avoid changing rewards,
   resets or the encoder to hide this execution-cost regression.
2. Corpus expansion to 64 games is complete. The new audit matches
   31,097/31,371 extended strategic regional signatures (99.13%) and fully
   labels 25,740/26,737 nonempty market queues (96.27%). There are 36 production
   overflow rows and one fully filled first market-prefix conflict, retained
   as visible limitations. Keep expanding in bounded cached batches, with
   stable episode holdouts; seven held-out games remain a small pilot, and
   the same display name still does not prove a fixed submission revision.
3. Compare fresh 2/2 PPO, BC-to-PPO, and BC+value-to-PPO against the retained 2/1
   baseline, with the same rewards, training resets and evaluation conditions.
   Include the user's reported successful BC -> eMAG + PPO recipe; active
   `train.emag_kl_coef=0` means the integration smokes did not test eMAG.
   Confirm the prior coefficients and GPU memory headroom in a bounded check
   before launching that branch. A loaded BC policy initializes the reference
   if no explicit magnet or `.emag` companion is supplied. Evaluate actual
   games, not just teacher-forced action accuracy, and retain standalone-weak
   BC checkpoints until their downstream benefit has been tested.
4. Port DAgger only after choosing and validating a callable expert. The old
   byte-observation `gen_dagger` is deliberately rejected by the v3 trainer.
   Replays cannot answer new learner-state queries. Existing native bot
   profiles are not a runnable Majkel1337 policy; replay/tape repair is not
   equivalent to querying that teacher. Aggregate learner-state labels with
   earlier data and select rounds by held-out closed-loop scores. Any value
   targets must describe the actual continuation used to generate them.
5. Test reset annealing and any old-reward variant independently. Regenerate
   derived returns when rewards/gamma change; retain expensive primitive tapes.
6. Implement policy-v5 export parity before a submission. The old 1,058-logit
   exporter cannot run this 1,978-logit controller.
