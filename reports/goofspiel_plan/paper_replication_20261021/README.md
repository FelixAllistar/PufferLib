# EMAgnet paper replication sweep, 2026-10-21

Goofspiel box (Vast 56827926, `ssh -p 13372 root@ssh2.vast.ai`), tree
`/workspace/PufferLib-paper` (copy of the mega tree, no reverse-KL commit), tmux `paper`,
launcher `run_paper.sh`, log `/workspace/goof_paper.log`. Report and resumable history:
`reports/goofspiel_sweeps/paper_replication_01/` (`sweep.history`).
Config: `make_paper_config.py config/goofspiel.ini config/default.ini` (original kept as
`config/goofspiel.ini.mega`).

## Setup vs. the paper (arXiv 2606.23995)

- Budget: 2M environment steps (Figure 2b x-axis) = 4,194,304 agent steps, fixed.
- Searched, paper Table 2 ranges: magnet coef 0.01–32, tau 1e-5–0.1, entropy 1e-4–0.1,
  entropy anneal on/off, LR anneal on/off, clip 1e-4–0.4, GAE lambda 0.6–0.999, value coef 0.1–5.
  LR uses our Muon range 1e-5–0.05 (the paper's 1e-7–0.01 is for its optimizer).
- Also searched, because the paper does not report it: rollout shape (agents 64–4096,
  horizon 4–32, minibatch 256–8192, replay 1–8).
- Fixed: current-policy self-play only (no history opponents, no exact exploiter), constant
  magnet (no anneal), forward KL, magnet starts as a copy of the learner, V-trace off, no
  reward clipping, synchronous, 128x4 PufferNet, anneals go to 0. Every other dimension is
  pinned (min = max) at the mega config's value.
- 700 trials (the paper used 700 samples x 3 seeds); one seed per trial here.
- Score is the best exact exploitability over the 4 checkpoints of the learner and the magnet.
  The paper reports the final checkpoint, learner and magnet separately.

## Mega sweep paused

Stopped after 427 PufferNet trials (best 0.0175, trial 335); the transformer phase never
started. It had `history_path = None`, so it cannot resume directly. All per-trial configs
(`logs/goofspiel/*.ini`) and scores (`sweep.log`) are kept, so a history file can be rebuilt.

## First results

Trial 0 (512 agents, horizon 8, minibatch 1024, replay 4, magnet 1 / tau 0.01): 0.080 in
14 s. For comparison, mega-sweep trials with budgets of 6M or less averaged 0.51.

## Disk-full crash and resume (2026-10-21)

The first run (`paper_replication_01`) crashed after 27 completed trials. The 40 GB disk filled, and
36 GB of it was the mega sweep's checkpoints. Those were deleted, except trial 335; every mega config
and score log was kept.

The 96 failure records written after the disk filled were dropped, and so was the truncated last
record. The 27 real results are in `reports/goofspiel_sweeps/paper_replication.history`. The sweep
resumed from that file into `paper_replication_02` ("restored completed=27"). Each trial uses about
6 MB, so 700 trials fit easily.

## Multi-seed confirmation of the top 5 configs (2026-10-21)

`run_seeds.py` reruns each trial's exact config with 8 seeds (`base.seed` 1–8, `env.seed` 1001–1008)
and no early stop, to 2M environment steps. Results are in
`reports/goofspiel_sweeps/seed_confirm_01/results.json`, as mean ± 95% CI (Student t, n=8).

| Config (sweep score) | Final learner | Final magnet | Best checkpoint |
|---|---|---|---|
| 442 (0.0324) | 0.0386 ± 0.0045 | **0.0366 ± 0.0040** | 0.0349 ± 0.0032 |
| 634 (0.0289) | 0.0416 ± 0.0054 | 0.0391 ± 0.0055 | 0.0371 ± 0.0045 |
| 402 (0.0322) | 0.0526 ± 0.0077 | 0.0468 ± 0.0070 | 0.0406 ± 0.0049 |
| 430 (0.0285) | 0.0558 ± 0.0062 | 0.0475 ± 0.0067 | 0.0309 ± 0.0033 |
| 438 (0.0279) | 0.0587 ± 0.0171 | 0.0548 ± 0.0166 | 0.0433 ± 0.0068 |
| Paper, PPO-EMAg (n=24) | 0.015 ± 0.011 | **0.007 ± 0.005** | n/a |
| Paper, uniform linear | 0.031 ± 0.009 | n/a | n/a |

- **Selection bias:** the sweep's 0.028–0.032 were lucky single seeds. The real means are 0.037–0.055
  for the final magnet.
- **Gap to the paper:** our best mean (0.037) is about 5× the paper's magnet result. It is also worse
  than the paper's own entropy-only baseline. That is a systematic difference, not sweep noise.
- **Untested differences:**
  - The paper's network, optimizer (we use Muon) and observation encoding are not stated in the
    text. Ours is a 128×4 recurrent PufferNet on a compact egocentric observation.
  - The "environment step" definition is assumed to be 2 agent steps.

## Setup ablations on config 442 (2026-10-21)

New default-off trainer switches, in the Goofspiel box's paper tree only:
- `train.optimizer = 1`: Adam with β1 = momentum and β2 = 0.999.
- `policy.memoryless = 1`: zero the MinGRU state every step in rollout and training, and zero the
  hidden state at every node in the exact evaluator.

The patches are in `ablation_patch/`. Every arm uses `puffer_ablate` (`build.sh goofspiel puffer_ablate --cu --float`).
The runner is `run_ablation.py`; results are in `reports/goofspiel_sweeps/ablation_01/results.json`.
Values are mean ± 95% CI over 8 seeds at 2M environment steps.

| Arm | Final learner | Final magnet | Best checkpoint |
|---|---|---|---|
| Baseline (Muon, recurrent, compact obs) | 0.0386 ± 0.0045 | 0.0366 ± 0.0040 | 0.0349 ± 0.0032 |
| Adam, LR 3e-4 | 0.1376 ± 0.0238 | 0.1366 ± 0.0241 | 0.1228 ± 0.0095 |
| Adam, LR 1e-3 | 0.0408 ± 0.0048 | 0.0390 ± 0.0044 | 0.0381 ± 0.0037 |
| Memoryless (feedforward) | 0.0373 ± 0.0027 | 0.0358 ± 0.0021 | 0.0349 ± 0.0017 |
| OpenSpiel observation tensor | 0.0519 ± 0.0074 | 0.0499 ± 0.0076 | 0.0437 ± 0.0052 |
| Paper, PPO-EMAg | 0.015 ± 0.011 | 0.007 ± 0.005 | n/a |

- **Binary:** the new binary reproduces the old one exactly (identical baseline numbers), so the old
  one was also built `--cu --float`.
- **No single cause:** none of the three changes explains the 5× gap. Adam at a reasonable LR and the
  feedforward net match the baseline within noise, and the OpenSpiel tensor is worse.
- **Caveat:** config 442 was tuned for Muon, recurrence and the compact observation, so each
  alternative is run at the baseline's hyperparameters. A small effect could be hidden, but not a 5×
  one.

## The old tuned configs (2026-10-21)

The user remembered about 0.007 from an earlier PufferLib run with the magnet on, at 30–50M steps.
`run_5c.py` reruns old configs on current code (`puffer_ablate`), with 8 seeds and final values. The
old keys map as `emag_kl_coef`/`emag_tau` → `magnet_coef`/`magnet_tau`. Frozen banks map to selfplay
history banks.

| Config | Steps | Final learner | Final magnet |
|---|---|---|---|
| 5c tip (32×2, 8,192 agents, 4 frozen banks at 50%), smoke, 1 seed | 50M | 0.091 | 0.122 |
| 5c tip on **5c's own code** (`/workspace/PufferLib-5c`), seed 73 | 50M | 0.065 | 0.127 |
| Aug 1 (`797ae7550`: 64×2, 2,048 agents, pure self-play, γ=λ=1) | 32.8M | 0.0244 ± 0.0041 | **0.0169 ± 0.0006** |
| Aug 1 with γ/λ = 0.998 | 32.8M | 0.0243 ± 0.0035 | 0.0167 ± 0.0008 |

- **Code vs settings:** the 5c config is bad on old and new code alike, so the code is not the
  regression. The settings drifted.
- **Discounting:** γ/λ = 1 vs 0.998 makes no difference.
- **Best known recipe:** the Aug 1 config, 2× better than the replication sweep's best. Its magnet is
  clearly better than its learner, as in the paper.
- **Still open:** it is not 0.007. That may need the Aug 1 code itself; that run is in progress.

**The Aug 1 code itself** (`db6b69375`, `/workspace/PufferLib-aug1`, default build, its own config:
CPU env, async, 2 buffers), 4 seeds at 32.8M steps:

| Seed | Final learner | Final magnet |
|---|---|---|
| 1 | 0.106 | 0.059 |
| 2 | 0.110 | 0.061 |
| 3 | 0.090 | 0.050 |
| 4 | 0.109 | 0.061 |

The old code does worse than current code on the same settings (0.058 vs 0.017 final magnet), so
nothing regressed. The ~0.007 memory most likely comes from that era's sweep score. The score was
the minimum over all checkpoints and the magnet, for one lucky seed out of 40 trials. That is the
same selection effect that made the replication sweep's best read 0.028 against a real mean of
0.037. Best honest recipe so far: the Aug 1 settings on current code, final magnet 0.0169 ± 0.0006
(n=8).
