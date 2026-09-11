# Explicit executor v1 — 2026-09-09

Opt-in replacement for the **remote mode-2** executor, not a new encoder or
reward experiment. Existing policies remain on executor 0. No policy is claimed
better until trained and compared on fixed root-start games.

## Division of responsibility

PPO chooses crop/species, quantities, planting quadrant, hiring, expansion,
selling, and explicit reclamation/fertilizing. It does not choose individual
worker movement. The native C/CUDA executor assigns eligible workers to jobs and
does recurring watering, feeding, care, harvest and completion of livestock
placement. Feed procurement is an automatic obligation for existing animals.

This is intentionally not “100% of every primitive learned by PPO.” It removes
scripted **farm strategy**, while retaining the automatic chores requested by
the user. Automatic non-ongoing crop harvest normally waits for peak maturity;
the explicit HARVEST intent permits an earlier ready harvest.

| Intent | Executor 1 behavior |
| --- | --- |
| 0 HOLD | Continue operations; no new strategic purchase or automatic sale |
| 1–5 PLANT species | Acquire missing seeds and assign up to the requested planting batch |
| 6–8 ANIMAL species | Prepare affordable housing, acquire and place requested livestock |
| 9 EXPAND | Explicit land purchase only |
| 10–29, 32–34 | Explicit market/hire/harvest intents retain their identities |
| 30 MAINTAIN | Masked: redundant with automatic operations/HOLD |
| 31 DIVERSIFY | Masked: never invokes the legacy whole-farm strategy |
| 35 RECLAIM | Clear weeds/empty housing in the selected quadrant; never occupied assets or housing reserved for unplaced animals |
| 36 FERTILIZE | Apply owned fertilizer to live crops in the selected quadrant |

The old workload-based hiring cap, 16-direct-worker restriction, and scripted
endgame investment cutoff are removed for executor 1. Game affordability,
resource capacity, and configured maximum workers still apply. Existing version
0 behavior is retained, including its masks and planner.

## Scheduling changes and limits

- All feasible cells enter assignment before the quantity cap is applied.
- Select eligible worker/target pairs globally; prioritize feasible local work,
  then job urgency and Manhattan distance. Unavailable feed/livestock cannot
  reserve a worker for a no-op. Each worker has at most one command per turn.
- Claim target tiles and resource/batch budgets to prevent duplicate jobs and
  over-budget atomic plant batches.
- Limit new housing by already-owned stock plus affordable new animals.
- All explicit intents can finish placement of already-bought livestock.

This is a deterministic greedy matching heuristic, **not** a proof of globally
optimal scheduling. Decisions replan each turn; it does not add a persistent
multi-day job queue. Macro quantity is a per-decision batch, not a desired final
herd count. Animal micro-location is still executor-selected. Exact tile choices
would need a spatial observation/action design; planting quadrant remains PPO's.
No Python executes in the GPU rollout path.

## Versioning / initial rollout (2026-09-09)

Only the remote active `config/kaggriculture.ini` is configured for this test:

```
base.run_id = explicit_executor_v1_fresh_20260909
base.load_model_path = None
env.macro_mode = 2
env.macro_executor_version = 1
env.frozen_macro_executor_version = 0
```

All rewards, LR, agents, minibatch, horizon, steps and opponent weights are left
as found. The existing obs1 league stays unchanged and uses executor 0. Mixed
executor training requires a nonempty external league and `opponent_pool_prob=1`
so new snapshots cannot accidentally enter old-executor banks. Live mirrors use
executor 1. End-of-run self-play evaluation associates versions with policies
in both seat directions, including rolling snapshots.

Every newly saved learner checkpoint receives `.obs_version` and
`.executor_version` sidecars. Version-aware GPU matrix/screen evaluation groups
both contracts. PSRO copies metadata, and homogeneous new-executor leagues can
iterate; mixed versions can analyze but cannot be silently merged into one
training bank contract. Mixed-contract behavior JSD is explicitly unavailable:
payoff-only diversity uses marked solver placeholders, not measured zero JSD.
Homogeneous JSD uses the selected observation/executor versions.

Native watch reads sidecars/run configuration and refreshes each policy's view.
CPU behavior diagnostics now retain macro context across turns; old behavior
reports that used the stateless helper should not be treated as native parity.

**Kaggle export implemented 2026-09-10:** `submission/native_macro_executor1.py`
ports the native worker/job allocation, resource budgets, explicit markets, and
legality masks. `NativeMacroRuntime(executor_version=1)` selects it without
changing executor-0 behavior. Metadata carries observation version, executor
version and sampling default; a missing executor-1 runtime is an import error,
never a silent primitive-policy fallback. Executor-1 quadrant masks deliberately
allow every unlocked quadrant, including full ones needed for reclamation and
fertilizing. The export supports the tested standard 10x10, 720-turn rules with
240 maximum hands, 100 shed capacity, and 10 market slots.

Use `KAG_DETERMINISTIC=0` for a stochastic archive (default remains deterministic):

```bash
KAG_PYTHON=/venv/main/bin/python KAG_DETERMINISTIC=0 \
  bash ocean/kaggriculture/package_native_macro_model.sh CHECKPOINT OUTPUT.tar.gz
```

Parity for the requested 999.29M executor-1 champion was checked against the
actual remote native source: 5,760 full observation/mask comparisons and 18,432
action comparisons over four 720-turn episodes (two seeds, both learner seats).
Additional native fixtures cover fertilizer, twenty hands, reserved housing,
all quantity/target combinations, and sale-price ties. Full stochastic export
smoke games ran in the Kaggle environment in both seats for seeds 7 and 42.
These are fidelity/runtime checks, not a claim of universal strategy optimality.
Reproduce with `tests/check_executor1_export.py` and
`tests/executor1_export_fixtures.c`; logs live under
`logs/kaggriculture/executor1_export_20260910/`.

The active league after PSRO is now `saved/kaggriculture_league_exec1_low_entropy_v1`.
All its members use executor 1; its selected 999.29M seed checkpoint retains that
version despite the historical `exec0` text in the run ID. The export work does
not modify the trainer, active config, rewards, or league weights.

## Validation / rollout

- Focused C tests cover local plant/place, masked DIVERSIFY fallback, reclamation,
  fertilizer, housing affordability, atomic seed budgets, no unsolicited
  investments, and policy-controlled hiring beyond the old cap.
- Persistent diagnostic context test verifies version association and previous
  macro state; scripted explicit requests produce milk over a full episode.
  This is a capability check, not a learned policy score.
- Host contract tests cover external/rolling opponents, reversed seats, restore,
  and unsafe mixed-pool rejection. Python tests cover pair completeness,
  contract grouping, checkpoint identity and metadata precedence.
- Remote CUDA adapter test includes legacy, new/new and new/legacy cases,
  both physical learner seats, comparing state/obs/mask/reward/reset/logs against
  the CPU execution of the same rules over 1,440 turns per case.

Remote test sources/logs and pre-change backups are kept in
`logs/kaggriculture/explicit_executor_v1_20260909/`.

Rebuild the trainer before using the opt-in config:

```
cd /workspace/PufferLib
./build.sh kaggriculture --gpu
./puffer train kaggriculture
```

No training, tmux session, league promotion or submission was started by this
change. The stopped prior trainer was not signaled. GPU inference/learning
throughput and learned-policy quality are not established by parity tests.
