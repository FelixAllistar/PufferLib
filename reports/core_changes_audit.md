# Trainer core changes audit

This file tracks every change outside `ocean/<env>` and `config/<env>.ini`, meaning the shared trainer (`src/`),
`build.sh`, `config/default.ini` and shared tests. **Add a row before committing any new core change.**
For each change it records what it does, which env needed it, and whether other envs are affected.

Branch policy:
- `5.0` is the main research branch and holds everything. `magnet-kl` was merged into it on 2026-10-23.

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

**Pokemon box 56007315 runs the `magnet-kl` trainer, not `5.0`.** Its tree is:
- `magnet-kl`'s `src/` and `config/default.ini`;
- plus the `PUF_LIVE_EVAL_*` hooks above in `src/pufferl.cu` (48 lines);
- plus `ocean/pokemon`.

Its Pokemon recipe trains with magnet KL (`train.magnet_coef = 0.1`), and it has none of the Protein changes.
The Transformer A/B box uses the same tree
(`pokemon_experiments/20261022_transformer_ab/README.md`).

## On local `5.0` and the Pokemon A/B box's `magnet-kl` tree (2026-10-22, local 5.0 not pushed)

| File | Change | Needed by | Default effect on other envs |
|---|---|---|---|
| `src/pufferl.cu` | `PUF_POLICY_ARCH(frozen)` hook, called before each policy's `build_arch` and once after the loop (restoring the learner's settings for the async actor and magnet copies). An env can switch its encoder per policy | Pokemon: Transformer learner vs frozen PufferNet (MLP) league banks | None: compiled out unless the env defines the macro |

The same 9 lines go into the A/B box's `magnet-kl`-based `src/pufferl.cu`
(`pokemon_experiments/20261022_transformer_league/box_pufferl_policy_arch.diff`). The Pokemon B1 box
(56007315) is unchanged.

Verified locally:
- Frozen banks get the MLP encoder: the bank files are 3,616,896 bytes, exactly the MLP parameter count.
- The learner checkpoint has the Transformer size.
- The trainer logs "13 frozen bank slots".

`puf_load_weights_into` has no size check for Pokemon, so a mismatched bank would load silently. This
was checked by hand here.

## `src/native_transformer.cuh` speed-up (2026-10-22, local 5.0 not pushed; also on the A/B box)

This is the shared Transformer used by Goofspiel (`policy.architecture=transformer`) and the Pokemon attention encoder.
The math is unchanged: only the kernel launch shapes differ, so results match to floating-point
reduction order.

| Change | Why |
|---|---|
| LayerNorm forward/backward and attention softmax forward/backward run one 32-lane warp per row (`*_warp` kernels; lanes stride the row) instead of one 256-thread block per row | At Pokemon's shape (21-token softmax rows, 128-wide LN) about 90% / 50% of the threads were idle, and every row paid block barriers. The old kernels were 23% + 18% of the step |
| Warp-row grid and row index use 64-bit math | Large shapes could overflow `rows × 32` |
| `affine_gradients` sums 256 rows per block before its atomics (was 32) | Fewer atomics; no measurable change at Pokemon's shape |

Checks:
- `tests/bench_native_transformer.cu --test` passes locally (FP32, Pascal) and on the A/B box (BF16, Blackwell): FP64 oracle with
  finite differences at three shapes, BF16 vs FP32, and CUDA graph vs eager.
- The Pokemon encoder CPU/GPU parity and numerical-gradient tests pass with attention on (2×16×2, 1×32×4) and off.

Speed (5060 Ti, BF16, forward+backward):

| Shape | Before | After |
|---|---|---|
| Pokemon: 21 tokens, width 128, 2 layers | 28.7 ms | 19.2 ms |
| 8 tokens, width 64 | 5.6 ms | 3.1 ms |
| 264 tokens, width 256, 4 layers | 14.2 ms | 11.8 ms |

## Window Transformer memory network (2026-10-23, local 5.0 not pushed; also in the A/B deploy tree)

| File | Change | Default effect on other envs |
|---|---|---|
| `src/window_transformer.cuh` (new) | `policy.network = window`: causal window Transformer replacing MinGRU. State = the last K encoder outputs in the ordinary recurrent state tensor; per step, attention over K+1 tokens. Weight gradients go on the dW side stream like MinGRU | None: only built when selected |
| `src/algo.cu` | `Network.window_layers/window_width`; `build_arch(..., network_type, window_layers, window_width, window_heads)` with MinGRU defaults; includes the new header | None: defaults keep MinGRU |
| `src/pufferl.cu` | `puf_resolve_network`: sets `policy.num_layers = policy.window_length` for window runs. Frozen policies use `vec.hist_policy_network` (`same`, `mingru` or `window`) | None: `network = mingru`, `hist_policy_network = same` |
| `config/default.ini` | `policy.network/window_length/window_layers/window_width/window_heads`, `vec.hist_policy_network` | Defaults are MinGRU |

Tests:
- `tests/test_window_transformer.cu`: training matches step-by-step rollout to 4e-7, with a non-empty starting window and mid-segment resets; 142 finite-difference gradient checks pass.
- `ocean/pokemon/tests/test_window_parity.cu`: GPU rollout matches the CPU evaluator path to 7e-7.
- A Pokemon smoke run (league, magnet, CUDA graphs) trains, and its checkpoint loads in the CPU evaluator.

Note: `base.cudagraphs = 0` still enables graphs; only a negative value disables them.

## Learner-row minibatch and step budget (2026-10-23, local 5.0 not pushed; also in the box deploy tree)

Background: since `f44130573` (Sep 27) PPO trains only on learner rows, and frozen-policy rows are
excluded. That is correct for on-policy PPO, and it won a one-seed Kaggriculture comparison (about 60k
vs 40k). It left two bookkeeping gaps:
- a sampled minibatch could stop dividing the shrunken learner batch. The current code asserted; the
  August snap (`d6f59a9da`) rounded silently and is no longer present;
- `total_timesteps` counts frozen rows too.

| File | Change | Default effect on other envs |
|---|---|---|
| `src/pufferl.cu` | `puf_resolve_minibatch`: at trial start, a minibatch whose sequence count does not divide the learner rows becomes the nearest valid size (logged; written to the config) instead of asserting | Only when the minibatch was invalid, which used to crash |
| `src/pufferl.cu` | `TrainResult` appends `minibatch_size` and `learner_steps`. The sweep parent accepts the legacy 784-byte packet (Python workers) or the new one, and observes a trial at the minibatch it actually used (clamped to the range) | Legacy workers unchanged; sweeps record true values |
| `src/pufferl.cu` | `learner_steps` logged every log step; `puf_train_epochs` shared by training and the dashboard | Extra log key only |
| `src/pufferl.cu`, `config/default.ini` | `train.budget = agents` (default, unchanged) or `learner`: `total_timesteps` counts learner rows only | None unless opted in |

Checks:
- An 85% Pokemon league run (590 learner rows, minibatch 512) used to assert; now it resolves to 320 and
  trains.
- `budget = learner` ran 283,200 learner steps (15 epochs × 590 × 32).
- `tests/check_sweep_result_packet.sh`: legacy packets are accepted, and a reported minibatch is
  recorded in the history.
- Builds pass for pokemon, breakout, poker, goofspiel (`--cu --float`) and kaggriculture (direct-policy
  ABI assert).

## Branch sync (2026-10-23): magnet-kl and upstream merged into local 5.0

The user's direction is to keep everything in one tree. Local `5.0` now contains:

- **`magnet-kl` (merge `3a3c94491`).** EMAgnet (`train.magnet_*`, `base.magnet_path`), off by default
  (`magnet_coef = 0`). It also removes Kaggriculture's native frozen-teacher KL. The Pokemon boxes'
  hand-assembled trees are now a subset of 5.0, so future deploys come straight from 5.0.
- **`upstream/5.0` (PufferLib PR #693, cuBLAS per-stream handles).** Each stream gets its own cuBLAS
  handle and workspace (`cublas_init_stream`); a GEMM on an unregistered stream asserts. Conflict
  resolution: our split `vec_thread_main` registers its rollout stream with `cublas_init_stream`
  (upstream's approach), instead of calling `cublas_init_handle`.

GPU checks on merged 5.0:
- window Transformer: training matches rollout, GPU matches CPU, gradient checks pass;
- Pokemon encoder parity (attention on and off);
- `tests/check_magnet.sh` (5 Goofspiel EMAgnet trainings);
- short trainings: poker (async), Pokemon MinGRU / entity attention / window with the league, and the
  Goofspiel Transformer (exact evals).

Breakout trains, then its end-of-training eval needs `env/draw_rate`. That is upstream `bde2eb70b`
("Unify eval", July) and predates this merge.

## Optional `env/draw_rate` in eval (2026-10-23, local 5.0)

| Files | What | Affects other envs? |
|---|---|---|
| `src/pufferl.cu` (`eval_loop`) | The final eval read `env/draw_rate` with `dict_get`, which exits when the key is missing (upstream bde2eb70b "Unify eval"). Envs without draws (poker, breakout) therefore failed every sweep trial after training. It now reads 0 when the env does not log it. | No: envs that log `draw_rate` get the same value |

## Box deploy (2026-10-23): both Pokemon boxes now run local `5.0`

Window box 57306307 and B1 box 56007315 were synced to local `5.0` at e3c68f02a (src, ocean/pokemon,
config, tests, vendor, scripts, build.sh) and rebuilt (`./build.sh pokemon`, `private_adapter.so`).
Before syncing, every line that existed only on a box was checked: all were older versions of code `5.0`
has replaced (cuBLAS handles, step budget, sweep packet, draw_rate). Previous sources:
`/workspace/before_5.0_sync_20261023.tgz`, `puffer.before_5.0_sync`, `private_adapter.so.before_5.0_sync`.

## Sweep robustness (2026-10-25, local 5.0)

| Files | What | Affects other envs? |
|---|---|---|
| `src/sweep_history.h` | A history whose parameters or format differ is moved aside (`path.old-<time>`) and a fresh one started, instead of exiting; missing parent directories are created. | Only sweeps with a history |
| `src/pufferl.cu` (`run_sweep`) | `history_path = None` now writes an automatic `<log_dir>/<env>/sweep_<time>.history` (downsample 1), so results survive a crash. Each worker's stderr goes to `<log_dir>/<env>/<run_id>.err` (deleted on success) and its last lines are printed on failure. The sweep stops after `sweep.max_consecutive_failures` (default 10) failures in a row instead of spending `max_failures` samples. | All sweeps; trial stderr no longer prints to the terminal (it is in the .err file) |
| `config/default.ini` | `[sweep] max_consecutive_failures = 10` | Default for all sweeps |

Motivated by a poker sweep that ran 73 good trials, then failed ~1,000 trials in a row (probably GPU memory taken by Windows apps on a 3 GB card) with every error lost to terminal scrollback.

## Pending / planned

- Pokemon entity Transformer (2026-10-21): **no core change.** Everything is in `ocean/pokemon/`. It reuses
  `src/native_transformer.cuh` unchanged (read-only `#include`). Off by default
  (`policy.pokemon_attention_layers = 0`).
