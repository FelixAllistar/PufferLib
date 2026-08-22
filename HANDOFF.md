# Kaggriculture Handoff

Last updated: 2026-08-21. This is the living "what have we done / where are we /
where next" reference for the Kaggriculture effort inside PufferLib.

## 2026-08-15: kaggle-environments 1.32.7 balance change (landed)

Upstream PR 1399 adds a `hinge` scarcity curve (`u + 8*max(0, u-1)^2`, `u=x/T`)
for CARROT (below_target 1.00), TOMATO (0.40), and EGG (0.40). Below the knee
the tomato/egg curves are identical to the old linear; carrot's changes. The
venv is pinned to 1.32.7, `reference/kaggriculture.py` is re-synced, and the
native core has `KG_FUNC_HINGE` with matching host/device defs.

Verification: local `make parity` passes including a new 719-frame
town-consumption scarcity case that pushes carrot/tomato/egg far past their
knees; remote `make cuda-core` passes 32 cases x 720 exact turns plus the
official price vectors. The remote training binary still needs a rebuild
(`./build.sh kaggriculture --gpu`) before prices change in training.

## Project goal

Train a competitive Kaggriculture agent with the native C/CUDA PufferLib
simulator, evolve it with self-play / PSRO / behavioral cloning, and submit the
best deterministic policy to the Kaggle competition. The competition objective
is terminal money over a 30-day (720-turn) season. For research, the dashboard
`score` is terminal marked player potential; terminal cash is `money`, and
realized harvested production is `gdp`. Inspect all three before calling a
policy healthy.

## Where the code lives

Everything Kaggriculture-specific is under `ocean/kaggriculture/`. Shared
training plumbing lives in `src/pufferl.cu` and `src/algo.cu`.

Layer boundaries:

- `kaggriculture_core.c` / `kaggriculture_core.h` — the rule engine, parity
  tested against the installed `kaggle_environments` interpreter.
- `kaggriculture.h` — observation encoder, conditional 47-head action tree,
  action masks, reward computation, native bot orchestration.
- `kaggriculture.cu` — GPU-resident environment and frozen-bank match path.
- `kag_bc.cu` — standalone behavioral-cloning / DAgger trainer.
- `kaggriculture_bots.h`, `kaggriculture_public_bots.h`,
  `kaggriculture_tape_data.h` — fixed tapes and adaptive planners used as
  self-play opponents and BC experts.

The policy is a MinGRU. The elite-replay branch uses a 1280-byte semantic
encoding and 47 heads / 1058-mask conditional action tree: farmer, sixteen
independent hands, and ten conditional market slots. Historical packaged
submissions retain the old 1024-byte / 42-head ABI; their checkpoints are not
load-compatible with the elite branch.

## Current champion

`saved/kaggriculture_league_v6/run_kag_v19_0000000909639680.bin`

- 128-wide, 2-layer MinGRU
- kag_v19 @ 909.64M
- confirmed at ~0.997 mean score / ~41.6k mean money against the active league
- packaged and submitted as `pufferlib_kag_v19_909m_v6.tar.gz`
- export smoke test: ~46-53k money across four seed/seat combos

This is the model to stand behind. Every 512x3 experiment so far has collapsed
below it.

## Champion lineage

The current recipe came from a chain, not one run:

1. short cash-ranked sweeps against native bots produced the v1 league;
2. the v8 / v10 / v19 lineage was warm-started continuation + PSRO promotion;
3. run 53 of the reward sweep produced the winning training recipe;
4. sweep 173 @ 18.35M was re-confirmed as the best mid-run checkpoint;
5. v19 continued from sweep-173@18.35M and beat all prior league members.

The proven hyperparameters (v19):

```ini
policy.hidden_size = 128
policy.num_layers = 2
frozen_bank_hidden_size = 128
frozen_bank_num_layers = 2
total_agents = 2048

learning_rate = 0.000553896592
ent_coef = 0.000281835062
emag_kl_coef = 0.0121208057
emag_tau = 0
emag_cutoff = 0.333
minibatch_size = 8192
horizon = 64
total_timesteps = 1000000000

reward_potential_scale = 0.5
reward_potential_gamma = 0.99970
reward_money_scale = 1

opponent_league = saved/kaggriculture_league_v6/league.ini
opponent_pool_prob = 0.75
bot_opponent_fraction = 0.5
```

## Rewards: current system

There are only three settings. `reward_potential_scale` controls both dense
credit and final realizable-net-worth weight, `reward_potential_gamma` must
equal `train.gamma`, and `reward_money_scale` adds terminal own cash. Potential
is neutral accounting net worth: cash; seeds, planted crops, placed/unplaced
animals, and purchased land at cost; plus already-created product at
conservative cumulative sale value.
It assigns no speculative future yield and no direct action, maintenance,
neglect, win, margin, inactivity, land, crop, or animal bonus. The old reward
coefficient system was removed rather than left as another sweep surface.

The zero-terminal-potential 1024x2 cold-start ablation failed: it converged to a
narrow crop/sell loop with no land, animals, or opportunity response and lost
essentially every deterministic game against the mature 1024 league. Retaining
terminal potential makes `money + stuff` an actual objective rather than a
shaping term that algebraically cancels.

## Key hard-won findings

### The 3k attractor

In a two-player game where PASS keeps your 3000 starting bank, pure self-play
converges to "do nothing and draw." Sparse reward + symmetric self-play gives
zero gradient. Escaping it requires dense potential shaping plus an asymmetric
opponent (league/bots) that creates actual win/loss signal.

### score, money, and GDP are separate

`env/score` and `env/sweep_score` are terminal `kag_player_potential` for
the learner; `env/opponent_score` is the opponent's potential. Potential
includes cash, marked inventory, live crops/animals, yield, and land using the
configured asset weights. `env/money`/`opponent_money` are cash only.
`env/gdp`/`opponent_gdp` count harvested output at its market price; purchases
and resale are excluded. Strawberry and milk have dedicated unit/value fields,
which is useful for the recent simulator price changes.

The sweep metric may be `score` (preferred) or the compatibility alias
`sweep_score`. Population/match TSVs keep `mean_money` as actual cash. A farm
with high GDP but low cash may be healthy but price-depressed; a high potential
with low GDP may simply be holding assets. Compare the trio rather than using
cash alone.

We added `margin` as a third `eval_metric`: the seat-balanced terminal money
gap (`my_money - opp_money`). Use `selfplay.eval_metric = margin` if gap is the
preferred objective.

### Agent count / minibatch / horizon coupling

`total_agents` changes rollout parallelism, not update count. `minibatch_size`
sets update size. `total_timesteps` sets total data. The dangerous quantity is:

```text
steps_per_rollout = 0.75 * total_agents * horizon / minibatch_size
```

Too high and PPO takes many steps on stale data -> `clipfrac ~ 0.999`, KL in
the teens, collapse. The LR/entropy cosine schedule runs over rollouts:

```text
rollouts = total_timesteps / (total_agents * horizon)
```

so raising agents also compresses the annealing schedule.

### eval_agents rounding bug

`base.eval_agents` must be a multiple of 4 in the GPU match path. `run_eval`
does `eval_agents += (-eval_agents) % 4`, so 2 becomes 0 and silently sets
`total_agents=0`. Use 16, not 2.

### Log-normal sweep ranges cannot include 0

The sweeper validates the default value against `log10`, so any
`log_normal`-distributed knob needs a positive min and default. Use `uniform`
with `min=0` when exactly-zero is a valid sample.

### BC and DAgger

- Full-game BC from random init collapses at argmax. Warm-start from the
  96-step opening clone + more games fixes it.
- DAgger (roll the clone, relabel drifted states with the expert) corrects the
  compounding drift. It fixed the "places animals but never feeds them" failure.
- DAgger overfits past ~5 rounds: training loss collapses while validation loss
  rises. Round 5 of the 512x3 thunder clone was the best by actual behavior.
- The magnet only matters through `emag_cutoff * 720` steps, so a 96-step or
  240-step clone is sufficient; a full-game clone is not required for EMAg.

### Orientation bug

Public planner bots originally placed the opening herd at the far corner. The
real replays build the first pasture adjacent to the shed. Fixed to an L-shape
at (3,4),(4,3),(3,3),(2,4). This was a bot heuristic bug, not a sim coordinate
bug.

### The old v2 "curriculum"

`train_v2.sh` was a genuine staged task curriculum (120 -> 360 -> 720 episode
steps with skill gates). `RESEARCH.md` incorrectly claimed no task curriculum
existed. The old `checkpoint_gate.c` was tied to the dead 1156-byte ABI and has
been deleted.

## Scripts worth knowing

- `train_bc_ppo.sh` — frozen BC KL training (the v19 lineage recipe).
- `train_v3.sh`, `train_v3_512.sh`, `train_v3_512_magnet.sh` — two-stage
  behavior bootstrap then economic transfer.
- `bc_pipeline.sh`, `clone_bots.sh`, `clone_bots_full.sh`, `dagger_bot.sh` —
  clone generation and DAgger iteration.
- `eval_population.sh` — GPU matrix evaluation (the only eval path to use).
- `eval_sweeps.sh` — cross-evaluate many sweep runs against the league.
- `psro.sh` — iterate / analyze / runs.
- `package_model.sh` — package a checkpoint for Kaggle submission.
- `set_sweep_mode.sh` — flip the sweep objective between staged spaces.

CPU eval scripts (`eval_v3.sh`, `eval_v4.sh`, `profile_population.sh`,
`eval.c`, `bench_hotpath.c`, `checkpoint_gate.c`) were deleted. Never add a
CPU eval loop back; everything evaluation should go through
`eval_population.sh` / `./puffer league`.

## Environments

Local: WSL, `~/puffertank/pufferlib`, GTX 1060 3GB.

Remote Vast.ai instance (primary training box):

```text
host: ssh4.vast.ai
port: 16847
user: root
key: ~/.ssh/vast_id_rsa
workdir: /workspace/PufferLib
```

Connect:

```bash
ssh -p 16847 -i ~/.ssh/vast_id_rsa root@ssh4.vast.ai
```

Long runs live in tmux sessions on the remote. `ssh_tmux` is the user's
interactive session; training/eval get their own named sessions.

## Current state and known issues

- The active league (`saved/kaggriculture_league_v6`) is 128x2. There is no
  512x3 league, so a 512x3 run cannot be PSRO'd against it.
- Local and remote league files have diverged. The remote is authoritative;
  local `league.ini`/`learner.tsv` are stale.
- The git tree is dirty with uncommitted script additions/deletions and core
  `pufferl.cu` changes. Commit before more cross-machine syncing.
- Widening sweep ranges to `animal_value` up to 10 / `land` up to 10 lets the
  optimizer reward-hack (e.g. `animal_value=8`). Keep bounded ranges and
  re-confirm winners with PSRO before promoting.
- The 512x3 lineage keeps collapsing to ~1-3k. The warm-start-from-clone +
  continuation-hypers recipe worked better than the chore-shaping bootstrap.

## Next ideas

1. Create a 512x3 league (`saved/kaggriculture_league_v6_512`) so bigger-model
   runs can be PSRO'd within their own architecture.
2. Use `thunder_dagger5_h512_l3.bin` as the 512x3 EMAg magnet with the v19
   continuation recipe (warm start from clone, not fresh init).
3. Sweep with `selfplay.eval_metric = margin` to rank on the money gap instead
   of raw money.
4. Re-run the 512x3 two-stage bootstrap but drop the chore-shaping phase that
   collapsed; keep only warm-start + continuation hypers.
5. Generate a proper 512x3 full-game base via DAgger if a standalone strong
   big model is the target.
6. Commit, sync, and keep one authoritative league rather than diverging local
   and remote state.

## Submissions so far

- `pufferlib_kag_v8_399m_v6.tar.gz` (v8 @ 399.90M, 128x2)
- `pufferlib_kag_v10_1882m_v6.tar.gz` (v10 @ 1882M, 128x2)
- `pufferlib_kag_v19_909m_v6.tar.gz` (v19 @ 909.64M, 128x2, current best)

Submit with:

```bash
cd ~/puffertank
.venv/bin/kaggle competitions submit kaggriculture \
  -f pufferlib/ocean/kaggriculture/submission/<file>.tar.gz \
  -m "message"
```
