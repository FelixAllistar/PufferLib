# Goofspiel Aug 1 neighborhood sweep (2026-10-21)

Goofspiel box (`ssh -p 13372 root@ssh2.vast.ai`), tree `/workspace/PufferLib-paper`, tmux `aug1sweep`,
log `/workspace/goof_aug1sweep.log`. Report and resumable history:
`reports/goofspiel_sweeps/aug1_sweep_01/` (`sweep.history`).

- **Start point:** the original Aug 1 config (commit `797ae7550`) on current code. That is 64×2,
  2,048 agents, pure self-play (no frozen banks), 32.8M steps, LR 0.0086, entropy 0.0165,
  magnet 0.118 / τ 0.00095. Its 8-seed final magnet is 0.0169 ± 0.0006.
- **Search:** 10 dimensions around it. Budget 16–65M; magnet 0.03–0.5; τ 1e-4–1e-2; entropy
  0.004–0.05; LR 0.002–0.03; clip 0.015–0.15; value coef 0.3–2; replay 2–6; agents 1,024–4,096;
  width 32–128. Everything else is pinned. 300 trials.
- **Score:** `seed_worker.py` trains each trial with 4 seeds and reports the mean final magnet
  exploitability. A lucky seed or checkpoint cannot win.
- **Speed:** about 30–70 s per trial, so roughly 4–6 h for 300 trials.
- **Config:** `make_aug1_config.py config/goofspiel.ini config/default.ini` rebuilds it from
  `config/goofspiel.ini.mega`. The replication config is kept as `config/goofspiel.ini.paper`.
- **Binary:** the scheduler and trainer are `./puffer_ablate` (`GOOFSPIEL_TRAIN_BINARY`).

Trial 0, the unchanged Aug 1 settings, scored 0.01676, matching the separate 8-seed check.

## Results (2026-10-21)

- **Sweep:** finished all 300 trials. Best 4-seed mean final magnet **0.0095** (trial 248: 55.9M
  steps, clip 0.015, magnet 0.217). Runner-up 0.0104 (trial 85). The lowest single seeds are
  0.0065–0.0070, which is where the remembered "0.007" comes from.
- **PufferNet vs Transformer** (`reports/goofspiel_sweeps/xfmr_vs_puffernet_01/`, run by
  `run_5c.py`): trial 85's settings, 4 seeds per arm, magnet exploitability every 4M steps.

| Arm | Final magnet (mean) | At 8M | At 16M | At 24M |
|---|---|---|---|---|
| PufferNet | **0.0127** | 0.557 | 0.232 | 0.086 |
| Transformer, PufferNet's LR (0.0103) | 0.0145 | 0.340 | 0.106 | 0.045 |
| Transformer, LR 0.003 | 0.0218 | 0.495 | 0.116 | 0.043 |
| Transformer, LR 0.001 | 0.0166 | 0.584 | 0.176 | 0.056 |

- **Learning speed:** per step, the Transformer learns much faster early, at about half the
  exploitability through roughly 24M steps. Both end in the same place (0.013–0.015), and the
  Transformer's late values are noisier, with one 0.027 seed.
- **Hyper transfer (research question 1):** PufferNet's tuned LR was the best of the three LRs for
  the Transformer too. In this env, PufferNet hyperparameters transfer at least that far.

## Seed hunt and cards-as-actions Transformer (2026-10-22)

**Seed hunt** (`seed_hunt.py`, `reports/goofspiel_sweeps/seed_hunt_01/`): configs 248, 98, 154 and
267, 64 fresh seeds each, exact saved settings.
- 5 runs finished below 0.007; the best final magnet was **0.0064** (config 154, seed 161).
- Honest 64-seed means: config 98 **0.0098**, 248 0.0111, 267 0.0114, 154 0.0230.

**Cards-as-actions Transformer:** `policy.transformer_pointer = 1`, Goofspiel only, default 0. The
patch is in `pointer_patch/` and is built on the box as `puffer_pointer`.
- The network passes all 5 tokens instead of pooling the global one.
- The pointer decoder scores card j from token j and the value from the global token, with the same
  `(5, H)` weight as before.
- dW runs on the trainer's side stream, which graph capture requires.
- The exact evaluator applies the same rule.
- **Smoke run** (1 seed, 16M steps): magnet 0.044 vs 0.106 for the pooled Transformer and 0.232
  for PufferNet at the same step count.
- The pooled Transformer is not bit-reproducible run to run (0.129 vs 0.122 at 4M with the same
  binary), so off-mode parity was checked by code path rather than by bits.
- A 4-seed comparison at trial 85 settings is running in tmux `pointer`.

**4-seed result** (trial 85 settings, 54.5M steps; final magnet exploitability, and the mean curve at 16M):

| Arm | Final (mean) | Seeds | At 16M |
|---|---|---|---|
| **Transformer, cards-as-actions** | **0.0116** | 0.0143, 0.0108, 0.0091, 0.0122 | **0.092** |
| PufferNet | 0.0127 | 0.0132, 0.0103, 0.0106, 0.0169 | 0.232 |
| Transformer, pooled | 0.0145 | 0.0267, 0.0107, 0.0094, 0.0113 | 0.106 |

Cards-as-actions is the best final value and learns fastest per step. It is still about 14× slower per
step in wall-clock (350 s vs 24 s per run).
