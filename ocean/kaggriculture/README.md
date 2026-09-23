# Kaggriculture on upstream PufferLib 5.0

This is the training port onto upstream revision `6ffa5b10d`, not the accumulated
5c trainer. It supports the current **2/2 controller, entity observation v3,
policy ABI 5**: 1,424 observations, 47 heads, 1,978 action logits and the entity
encoder with separate actor/value branches around upstream MinGRU.

## Start training

On the prepared Vast install:

```bash
cd /workspace/PufferLib
./puffer train
```

The environment is compiled into the binary: do **not** append `kaggriculture`.
To rebuild: `NVCC_ARCH=sm_120 bash build.sh kaggriculture --cu`.
The ready profile loads `saved/kaggriculture/initial_bc_critic.bin`, uses terminal
cash gain only, a trainable critic, replay resets and frozen league opponents.
The 300M-step run is not automatically launched by installation.

Settings selected for the first upstream run:

| Setting | Value |
|---|---|
| Model | H256, 2 recurrent layers, 1,082,200 parameters |
| Initialization | Existing BC actor + terminal-return critic |
| Terminal reward | `4.71806717 * (ending_cash - episode_start_cash) / 3000` |
| Other rewards / clipping | All zero / disabled |
| Gamma | 0.999817491, matching critic targets |
| Replay resets | Probability 0.8; 78,864-state bank |
| Physical agent rows | 1,024: 640 learner, four banks of 96 opponents |
| Games | 512; 75% checkpoint games, 25% learner-mirror games |
| Rollout / minibatch | 256 steps / 8,192 transitions |
| RNN state | Carried across rollouts; reset on episode boundaries |
| PPO | Upstream losses, Muon, GAE, async pipeline; LR 0.0003 |

These are a qualified starting configuration, **not** an optimum transferred
from the old trainer. The horizon is shorter than the former 720; carrying
state preserves memory, not the full 720-step backpropagation window.
Upstream counts all physical agent rows in its step budget. At this league
split, 300M nominal steps contain approximately 187.5M learner transitions.
Minibatch *sequences* (`minibatch_size / horizon`) must divide the learner-row
count. Rollout masks are ordinary precision tensors, not bitpacked.

`reset_fraction` measures completed logged episodes, not necessarily the
configured draw probability: continuations are shorter. `root_money` and
`reset_money` separate genuine starts and continuations. Production/action
metrics exclude activity inherited from bank states. During critic-only offline
fitting, the encoder and actor stay frozen while the value branch trains.
During PPO both actor and critic train normally.

For the earlier shaped-reward experiment with actor-only BC:

```bash
uv run --no-project python ocean/kaggriculture/run.py train --profile shaped
```

The shaped profile restores the earlier land/crop/animal high-water bonuses,
alive reward and dense quality reward. It does not enable PBRS. Terminal reward
units, observations and controller semantics are unchanged.

## Evaluation and checkpoint league

Use the runner for reset-free evaluation; native post-training evaluation is
disabled in the starting config so it cannot inherit training reset states.

```bash
uv run --no-project python ocean/kaggriculture/run.py eval \
    --base.load_model_path=checkpoints/kaggriculture/RUN/CHECKPOINT.bin \
    --env.bot_policy=1 --env.learner_seat=0
```

Repeat with seat 1 and bot 0 (pass). Bot 1 is the native reactive rules bot,
not a top competition agent. Evaluation is stochastic, as in upstream's sampler.
A direct native `match` needs two checkpoints, two agents per game and resets off;
the league runner sets these correctly and balances both seats.

The prepared league retains the seven compatible legacy models and the legacy
training mixture, explicitly tagged as such. Its weights are **not claimed to
be a newly evaluated upstream PSRO solution**. Four fixed banks approximate that
mixture by seeded systematic resampling. Opponent weights stay fixed within a
run; leave `selfplay.opp_timeout_steps=0`. Mid-game bank replacement is not part
of the qualified workflow.

All payoff collection, Nash solving and champion selection live in `run.py`,
outside PPO. To add a finished run, evaluate missing pairs, promote the empirical
champion in the registry and refresh the next run's banks:

```bash
uv run --no-project python ocean/kaggriculture/run.py league-add \
    --name candidate_1 --checkpoint checkpoints/kaggriculture/RUN/CHECKPOINT.bin
uv run --no-project --with numpy --with scipy python ocean/kaggriculture/run.py \
    league-eval --games 64 --bots
uv run --no-project --with numpy python ocean/kaggriculture/run.py \
    league-sample --banks 4 --seed 708
```

`league-eval` caches completed pair results, so an interrupted matrix resumes.
It updates the registry's champion automatically, not the actor initializer in
your training config. To continue that champion, explicitly override
`--base.load_model_path=PATH`. Small match counts are noisy; promotion is an
empirical ranking, not proof of stronger competition play. `league-add` accepts
only this qualified H256/L2 contract; file size alone does not establish the
semantics of an arbitrary imported checkpoint.

## Offline BC and critic fitting

The preserved expanded dataset contains **442 training + 76 held-out games**.
No replay parsing was repeated for this migration. The installed terminal and
shaped data links reuse the immutable original files; do not delete `legacy`.

```bash
uv run --no-project python ocean/kaggriculture/run.py build-bc --arch sm_120
uv run --no-project python ocean/kaggriculture/run.py bc \
    --bc.output=saved/kaggriculture/new_actor.bin
uv run --no-project python ocean/kaggriculture/run.py critic \
    --base.load_model_path=saved/kaggriculture/new_actor.bin \
    --bc.output=saved/kaggriculture/new_actor_critic.bin
# Or simultaneous actor CE + value regression:
uv run --no-project python ocean/kaggriculture/run.py bc-critic \
    --bc.output=saved/kaggriculture/new_joint.bin
```

The native offline application uses the actual upstream encoder/MinGRU/decoder
vtables. It does not instantiate a simulator or alter PPO. Actor loss is masked
conditional cross-entropy, normalized by labeled rows. Value targets are expert
Monte Carlo returns in PPO reward units; only the MSE loss is divided by
training-return variance. Critic-only fitting changes only the value branch.
Actor-only fitting leaves value-branch weights untouched, although shared
features can change its predictions. The immutable episode split is retained.

The standalone offline optimizer is Adam; it is not the old actor-BC SGD
optimizer and is **not** a replacement for upstream PPO's Muon. The selected
initializer remains the existing qualified checkpoint, not a newly retrained
one. Dataset metadata checks controller settings and, for value training,
reward units/gamma before launching. Outputs refuse overwrite and receive
provenance JSON. `bc.max_batches=0` uses all games; nonzero is a smoke-test limiter.

New replay collection/relabeling and Kaggle export tools remain preserved in the
legacy install. They have not been folded into the new trainer. Existing data
and checkpoint loading do not imply new raw replay parsers were ported.

## Shared-code boundary

Following `SKILL_ISSUES.md`, simulator, controller, observations, rewards,
resets, entity network, offline tools and league orchestration stay under
`ocean/kaggriculture`. Shared runtime changes are limited to:

- `src/ocean.cu`: seven lines of existing custom-network vtable dispatch.
- `src/pufferl.cu`: warm loading, optional reward clipping, prefix-sampler
  context, GPU policy-row hookup, initial opponent paths, and selecting only
  learner rows (including initial recurrent states) for PPO.

The row-selection fix is data routing: frozen-opponent behavior is not used as
learner experience. `src/algo.cu`, Muon, PPO/advantage/value losses, MinGRU
implementation, async scheduling and `src/pufferenv.h` remain upstream.
The sampler retains action/mask/log-prob/RNG parity. Market heads not reached
by the selected prefix use singleton masks and probability one in the stock loss.

No eMAG, QD, packed rollout masks, old trainer accumulation or optimizer/loss
experiments were imported. Raw/2/1 controllers, renderer, CPU trainer adapter,
opening curriculum and optional historical environments remain in the preserved
legacy trees; they are not silently interpreted as the new 2/2 configuration.

## Qualification

CPU checks (no GPU execution):

```bash
uv run --no-project --with pytest --with numpy --with scipy python -m pytest -q \
    ocean/kaggriculture/tests/test_core.py ocean/kaggriculture/tests/test_policy.py \
    ocean/kaggriculture/tests/test_profiles.py ocean/kaggriculture/tests/test_league.py
```

Optional legacy comparison uses `KAGGRICULTURE_REFERENCE_ROOT=/path/to/legacy`.
Optimized and ASan/UBSan traces cover rules, observations, decoded commands,
conditional masks and exact sampler RNG state. The standalone rule core also
passed interpreter parity against the installed Kaggle 1.32.7 rules.

GPU suites are opt-in: `test_gpu.py`, `test_network.py`, `test_bc.py`,
`tests/test_train_checkpoint.py` and `tests/test_train_policy_rows.py`. They
exercise real native kernels, graphs, recurrent carry, sync/async warm loading,
frozen-bank invariance, reset baselines, rewards, independent NumPy network
gradients and real checkpoint recurrence in FP32 and BF16. Tests leave no model
promoted. Complete scripts/logs live in the migration qualification artifacts;
tiny test throughput must not be presented as production training throughput.

Production-size qualification on Vast's RTX 5060 Ti: the 1,024-row async profile
completed 1,048,576 steps at about 9.4 GiB, with a measured steady interval near
15.9K physical steps/s. Total wall time was 143 seconds including graph capture;
the dashboard excludes capture time and its final async drain shows a misleading
SPS spike. A 2,048-row synchronous comparison held the model and horizon fixed
but reached only 13.7–13.8K steady SPS at about 10.3 GiB, so it was not selected.
These short runs qualify operation, not convergence or a speedup over legacy.

The preserved legacy champion also completed a reset-free, balanced-seat native
match against `terminal_final`: 28/32 wins, mean learner cash 91,106. This is a
checkpoint/match sanity check, not a newly selected champion or leaderboard result.
