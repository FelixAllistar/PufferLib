# Magnet KL regularization and learner-row fixes

Trainer changes on this fork's `5.0` branch (none are upstream yet). Each commit is self-contained; port the
`src/`, `config/default.ini` and `tests/` parts. `f441305` also touches `ocean/kaggriculture`, `42be2e7` also
touches `ocean/goofspiel` and `reports/`: those parts are fork-specific.

## Magnet KL

| Commit | What it does |
|---|---|
| [`52ab9b2`](https://github.com/FelixAllistar/PufferLib/commit/52ab9b2100a2abf8b0e94dc86e8c5c1f9438b167) train: add magnet KL regularizer (EMAgnet) | Adds `magnet_coef * KL(magnet ‖ policy)` to the PPO loss. The magnet is a second copy of the policy that moves toward the learner by `magnet_tau` after each pass over the rollout; `magnet_tau = 0` keeps a fixed anchor (e.g. a BC policy, as in spiced self-play). Fused into the PPO kernel, optional cosine anneal, checkpoints save `<ckpt>.magnet`. Off by default and bit-identical when `magnet_coef = 0` (`tests/check_magnet.sh`). |
| [`0bab4ec`](https://github.com/FelixAllistar/PufferLib/commit/0bab4ec91e925dee25d537709212352f8570f2a1) train: reverse magnet KL and random magnet init | `train.magnet_reverse = 1` uses `KL(policy ‖ magnet)` (mode-seeking, as in Ataraxos). `base.magnet_path = random` starts the magnet as a fresh, near-uniform net. |

## Learner rows and minibatches (self-play with frozen opponents)

| Commit | What it does |
|---|---|
| [`49fdefa`](https://github.com/FelixAllistar/PufferLib/commit/49fdefa67bc95a00a21dca788add543a1aaeac80) fix(train): gather only primary-policy rollouts for PPO updates | Before this, rows played by frozen/historical policies were trained on as if they were the learner's own on-policy data. |
| [`f441305`](https://github.com/FelixAllistar/PufferLib/commit/f44130573ce5c552a1281ba3491b6329faa4dcaa) restore learner-only rollout gathering | Final version of the fix above, after an all-rows vs learner-only comparison (learner-only won). The revert in between, `7e030af`, was only for that comparison. |
| [`dea4d8f`](https://github.com/FelixAllistar/PufferLib/commit/dea4d8fb9d3b1c0955a7cc44ad453d720a007789) trainer: resolve minibatch to the learner batch | Its minibatch snapping (move to the nearest size that divides the learner rows) is **gone**: `42be2e7` replaced it with padding. Still live from it: opt-in `train.budget = learner` (`total_timesteps` counts learner rows only), the `learner_steps` log, and sweeps recording the minibatch a trial actually used. Needed as the base `42be2e7` applies on. |
| [`42be2e7`](https://github.com/FelixAllistar/PufferLib/commit/42be2e78e6ea6609e6908835a514fc0e7d43eec7) trainer: pad minibatches with zero-weight rows instead of resizing them | Rounds each minibatch up and gives the pad rows zero weight, so every learner row trains once per epoch. This is the current minibatch behavior. |

## Papers

- **EMAgnet**: Parameter-Space EMA Regularization for Policy Gradient Self-Play in Large Games. <https://arxiv.org/abs/2606.23995>
- **Magnetic Mirror Descent**: A Unified Approach to Reinforcement Learning, Quantal Response Equilibria, and Two-Player Zero-Sum Games (Sokota et al.). <https://arxiv.org/abs/2206.05825>
- **Ataraxos**: Scalable Decision Making for Games of Imperfect Information (Stratego; source of the reverse KL). <https://arxiv.org/abs/2511.07312>
- **Spiced self-play**: Human-like autonomy emerges from self-play and a pinch of human data (a fixed BC anchor = `magnet_tau = 0`). <https://arxiv.org/abs/2606.19370>
