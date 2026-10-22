# Trainer core changes audit

This file tracks every change outside `ocean/<env>` and `config/<env>.ini`, meaning the shared trainer (`src/`),
`build.sh`, `config/default.ini` and shared tests. **Add a row before committing any new core change.**
For each change it records what it does, which env needed it, and whether other envs are affected.

Branch policy:
- `5.0` is the main research branch.
- `magnet-kl` holds the EMAgnet trainer changes. Do not merge it into `5.0`.

## On `magnet-kl` (pushed to origin)

| Commit | Files | What | Affects other envs? |
|---|---|---|---|
| 04eb8a4e1, 43e8974db, 9cfff0fc8, ed830b848 | `src/algo.cu`, `src/pufferl.cu` | EMAgnet magnet-KL regularizer, reverse KL, random magnet init, explicit magnet path | Only when `train.magnet_*` is set; branch only |

## Patches only (not in any tree)

| Where | Files | What |
|---|---|---|
| `goofspiel_plan/aug1_sweep_20261021/pointer_patch/*.diff` | `transformer_policy.cuh`, `algo.cu`, `pufferl.cu` | Goofspiel Transformer pointer decoder (`policy.transformer_pointer`). Built only on the paper tree on the Goofspiel box, which is now destroyed |
| `goofspiel_plan/.../ablation_patch/` | `algo.cu`, `pufferl.cu` | Adam optimizer (`train.optimizer=1`) and `policy.memoryless` ablations |

## On local `5.0`, commit 016e71223 (2026-10-21, not pushed)

These were uncommitted working-tree edits from the Pokemon, Mario and Goofspiel work, committed together here.

| File | Change | Needed by | Default effect on other envs |
|---|---|---|---|
| `src/pufferl.cu` | `PUF_LIVE_EVAL_*` hooks: checkpoint + Metamon eval during training, shown on the dashboard | Pokemon (`ocean/pokemon/metamon_eval.h`) | None: compiled out unless the env defines the macros |
| `src/pufferl.cu` | `PUF_TRAIN_START` / `PUF_TRAIN_STOP` / `PUF_TRAIN_LOG` hooks; a stop also saves a checkpoint | Retro, mario_sim, Goofspiel | None unless defined |
| `src/pufferl.cu` | `PUF_SWEEP_RESOLVE`: env rewrites derived sweep params, and the sample is renormalized from the resolved values | Goofspiel (integer budgets) | None unless defined |
| `src/pufferl.cu` | `PUF_SWEEP_ACTUAL_BUDGET`: a stopped worker is observed at the steps it actually trained | Retro, mario_sim | None unless defined |
| `src/pufferl.cu` | Sweep resume calls `protein_sweep_restore_suggestions` instead of only setting `suggestion_idx` | All resumed sweeps | **Behavior change:** a resume no longer repeats the first Sobol random trials |
| `src/pufferl.cu` | Sweep start printf: `replay=%.0f` → `%.3g` | All | Log text only |
| `src/protein.cu` | `optimize_direction` applied to scores at observe/should_stop time, not only at candidate pick. Pareto 0.98 "near best" and the fallback threshold flipped for negative scores. Predicted score reported in user units | Goofspiel (minimizing exploitability) | **Behavior change for `sweep.goal=minimize` sweeps only.** The old code fit the cost model and early-stop on unflipped scores, which was wrong for minimize. Maximize sweeps: scores ≥ 0 are unchanged; the negative-score branches differ only when scores < 0 |
| `build.sh` | Retro links `-lz` | Retro (archive `.gz`) | Retro only |
| `tests/test_protein_numerics.cu`, `tests/test_protein_resume.cu` | Tests for the two Protein changes | n/a | n/a |

Commit 7c75de2b9 also touches the Goofspiel tests in `tests/` (`test_goofspiel_*`, `check_goofspiel_live.sh`), but they are env-specific.

**Pokemon box 56007315** runs its own deployed copy of these sources. B0/B1 use the live-eval hooks, and none of
the Protein changes, because they are train runs, not sweeps.

## Pending / planned

- Pokemon entity Transformer (2026-10-21): **no core change.** Everything is in `ocean/pokemon/`. It reuses
  `src/native_transformer.cuh` unchanged (read-only `#include`). Off by default
  (`policy.pokemon_attention_layers = 0`).
