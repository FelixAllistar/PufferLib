# Kaggriculture

## Native training and match viewer

### Native public-policy opponents

The downloaded Kaggle agents are not neural checkpoints. Their notebooks
contain frozen replay traces, sometimes wrapped in a large
AGENT_SOURCE = """...""" string, plus small state-dependent repair
functions. The native port keeps that distinction explicit:

- frontier and night use the same packed 720-frame Frontier tape.
- v20 uses the 719-frame V20 trace padded to the 720-turn season.
- moon uses the fixed public Subin/Frontier tape.
- hamburger/tran uses the fixed Tran H. Hoang anchor tape.
- fields, scenario, soil, kaito, shield, and frontier12 are compact
  state-driven C policies corresponding to the earlier adaptive notebook
  styles.
- pulse is a native Harvest Pulse/Goose Dividend planner: it maintains a
  wheat reserve, builds coops, buys geese, and replans sales around current
  prices.
- structured/economic is a native Structured Economic planner: it stages the
  COW/COW/COW/SHEEP opening, uses the notebook crop mix as a prior, schedules
  land at days 5 and 9, and replans worker jobs and orders from live state.
- triad/adaptive is the adaptive-farming trace port. Its notebook trace is an
  opening prior only; the native policy replans every turn and does not replay
  the 720 actions.

All tapes decode once at startup into bounded native arrays. The per-turn
executor emits the ordinary KGAction: up to 15 taped hands and 10 market
orders, with no Python, allocation, string parsing, or notebook code in the
hot path. V20/Moon/Hamburger apply only emergency weed, watering, and animal
maintenance repairs when the public replay drifts against a different seat
or seed. The learned policy's configured structural limits remain
independent; the headed native bots use the complete simulator controls.

Useful checks after a build:

~~~bash
./kaggriculture bench 720 frontier pass
./kaggriculture bench 720 night pass
./kaggriculture bench 720 v20 pass
./kaggriculture bench 720 moon pass
./kaggriculture bench 720 hamburger pass
./kaggriculture bench 720 pulse pass
./kaggriculture bench 720 structured pass
./kaggriculture bench 720 triad pass
./kaggriculture watch latest frontier
~~~

The training config samples these native tapes through `env.bot_script_fraction`
and the three dynamic ports through `env.bot_adaptive_fraction`. It reports
separate `env/script_*_fraction` and `env/adaptive_*_fraction` metrics. The C
adapter test validates every tape at all 720 frames, including the maximum
observed hand and market counts.

```bash
bash build.sh kaggriculture
./puffer train kaggriculture

bash build.sh kaggriculture --fast
./kaggriculture watch latest              # newest checkpoint vs itself
./kaggriculture watch latest rules        # newest checkpoint vs native bot
./kaggriculture watch A.bin B.bin         # two checkpoints
./kaggriculture watch carrot melon        # two native rule bots
./kaggriculture watch pulse structured   # two dynamic public-policy ports
./kaggriculture bench 100000 random rules # headless C benchmark
```

Viewer sides are `latest`, a checkpoint path, `rules`/`wheat`, `carrot`,
`melon`, `frontier`, `night`, `v20`, `moon`, `hamburger`, `fields`,
`scenario`, `soil`, `kaito`, `shield`, `frontier12`, `pulse`, `structured`,
`economic`, `triad`, `adaptive`, `random`, or `pass`.
`SPACE` pauses and `PERIOD` single-steps.

This port lives under `ocean/kaggriculture` because this checkout uses the
singular `ocean/` environment directory. It contains:

- `reference/kaggriculture.py`: the installed Kaggle interpreter used as the
  oracle, copied from `kaggle-environments==1.32.7`.
- `kaggriculture_core.c/.h`: a native structured-rule simulator.
- `kaggriculture.h`: the PufferLib adapter (two agents, compact observations,
  one farmer head, sixteen independent hand heads, and one sparse market
  tree).
- `parity.py`: frame-by-frame differential testing against the official
  Python environment.
- `eval.c`: a standalone native C evaluator for baseline-vs-baseline matches.

## Activate and verify the Python side

From the repository root:

```bash
source ../.venv/bin/activate
python -c "import kaggle_environments; print(kaggle_environments.__version__)"
kaggle --version
```

The competition reference bundle can be refreshed with:

```bash
kaggle competitions files -c kaggriculture
kaggle competitions download -c kaggriculture -p ocean/kaggriculture/reference
```

Kaggle authentication and joining the competition are account-level steps;
the CLI will report an authorization error until the competition rules have
been accepted on the competition page.

## Differential verification

The parity harness uses the official `make("kaggriculture")` environment and
compares every serialized field after every action frame, including farms,
private inventories, market state, town shops, daily refreshes, and the
deterministic weed/shop RNG stream.

```bash
make -C ocean/kaggriculture parity
make -C ocean/kaggriculture adapter
make -C ocean/kaggriculture eval
make -C ocean/kaggriculture native-test  # zero-Python adapter + C eval
make -C ocean/kaggriculture test  # native-test plus external parity
make -C ocean/kaggriculture bench
```

The benchmark is only a throughput indicator; `parity` is the correctness
gate. It runs both a scripted 48-frame scenario and a 160-frame randomized
scenario. Change the seeded scenario or add a focused scenario before changing
rules in the native core. Python is used only as this external oracle; it is
not part of the native simulator, trainer, evaluator, or headed demo.

## Build the PufferLib environment

```bash
./build.sh kaggriculture --fast   # compile the headed Raylib demo
./kaggriculture watch latest     # newest native checkpoint vs itself
./kaggriculture bench 100000 random rules
make -C ocean/kaggriculture eval # native C baseline evaluation
./build.sh kaggriculture          # compile the native C/CUDA trainer
```

The trainer uses the complete CPU-resident C environment with the CUDA policy
path. The elite-replay ABI has 47 conditional heads totaling 1058 logits:
seventeen 44-way unit heads (farmer plus sixteen independent hands), then ten
ordered market slots. Each market slot is a path through
STOP/CONTINUE (2), command (21), and quantity `{1,2,3,4,5,6,8,10}`. Sampling, PPO log probability,
entropy, KL, and gradients visit only the selected path. STOP suppresses all
later slots; HIRE and BUY_LAND suppress quantity. This preserves every official
ten-order queue and useful bulk quantity without the destructive entropy of ten
independent flat actions. Historical legality masks remain bit-packed on GPU.

The observation is exactly 1280 bytes (`uint8_t`). Its CUDA encoder normalizes
bytes before one fused dense projection. Public production is represented by
per-quadrant entity/lifecycle counts and per-product crop/animal summaries;
private shed/seeds and full controlled-unit inventories are explicit. Every
controlled unit also receives one-hot local tile state and egocentric routes to
maintenance, harvest, empty land, weeds, and shed. Opponent positions and public
production remain visible, while opponent private inventory remains absent.
Market supply is signed deviation from equilibrium. The semantic ABI avoids
forcing a dense MLP to rediscover categorical meaning from ordinal tile IDs.

Official elite-replay imports retain one manifest row per recurrent player
trajectory. A mixed-agent clone can average incompatible strategies, so whole
agent lineages can be sliced without changing section order:

```bash
python3 ocean/kaggriculture/subset_bc_dataset.py \
  /workspace/elite_replays/kaggriculture_elite_1.32.7.bc \
  /workspace/elite_replays/crop_dusta.bc --agent 'Crop Dusta'

# A coherent opening-only curriculum dataset uses the same section-safe copy.
python3 ocean/kaggriculture/subset_bc_dataset.py \
  /workspace/elite_replays/kaggriculture_elite_1.32.7.bc \
  /workspace/elite_replays/crop_dusta_open26.bc \
  --agent 'Crop Dusta' --prefix-steps 26
```

The resulting BC file and `.players.tsv` sidecar contain only complete
720-step trajectories and can be passed directly to `train_elite_bc.sh` via
`KAG_ELITE_BC_DATA`. `KAG_ELITE_BC_OPENING_WEIGHT` weights all configured
opening rows; `KAG_ELITE_BC_ROOT_WEIGHT` can separately emphasize the first
decision, where one argmax error otherwise shifts the entire recurrent
trajectory off demonstration. For deterministic clones,
`KAG_ELITE_BC_OPENING_ARGMAX_COEF` enables a hinge term on opening heads whose
expert logit is not ahead by `KAG_ELITE_BC_ARGMAX_MARGIN`; both default to zero
so ordinary cross-entropy behavior is unchanged.

The training dashboard uses a Kaggriculture-specific diagnostic view: score,
opponent money, win/draw rate, expansion/useful extra tiles, watering and
neglect, unused seeds/weeds, live plants/animals, market orders per turn,
buy/sell/hire activity, and animal feed/care/harvest/fertilizer activity. The
full raw counters remain available as `env/*` metrics for scripts; redundant
generic fields are no longer forced into the visible panel.

“Adapter” here means only the bridge from Puffer’s flat discrete policy heads
to Kaggriculture’s structured action dictionaries. It does not make hands
passive or change the rule core; the native core remains the parity-tested
structured simulator.

The V4 action ABI is intentionally incompatible with all V1–V3 checkpoints,
and the new 256-wide policy is kept in its own V5 league. Bootstrap a fresh
full-rules population with:

```bash
./build.sh kaggriculture
./puffer train kaggriculture base.run_id=kag_v5_256_seed101 base.seed=101 \
    env.seed=101 selfplay.seed=101 train.total_timesteps=100000000
```

The first V4 run is a fresh full-rules bootstrap. Rank its trajectory with:

```bash
./ocean/kaggriculture/eval_v4.sh RUN_ID 50
```

Then build the compatible V5 league with seat-balanced PSRO:

```bash
./ocean/kaggriculture/psro.sh iterate --run checkpoints/kaggriculture/RUN_ID \
    --range 0:100:16 --games 50 --confirm-games 500
```

PSRO rewrites the active V4 learner and opponent pool in the config. Subsequent
responses can continue with `./puffer train kaggriculture`; use a new `base.run_id`
and seed when starting a separate trajectory.

The current config uses per-player, discount-consistent potential shaping. Its
potential is deliberately neutral accounting net worth: cash; seeds, live
crops, live/unplaced animals, and land at cost; and already-created products at
their conservative cumulative sale value. It does not guess future yield or
award actions, maintenance, wins, margins, land, animals, or plants directly.
The real terminal potential is retained, so when `env.reward_potential_gamma`
equals `train.gamma`, its dense deltas telescope to final realizable net worth.
Normalized final own cash (`env.reward_money_scale`) is added separately to
favor realization over merely holding assets.

Kaggriculture disables the generic transition reward clamp with
`train.reward_clip=0`, so better final cash remains better at every scale. A
large product pile is marked by its cumulative realizable revenue rather than
pretending every unit can sell at the first unit's quote.

### Positive elite future-value reward

`env.reward_progress_scale > 0` enables the alternative nonnegative objective
used for fresh exploration experiments. Its fitted state value is:

```text
cash
+ conservative paid cost of uncommitted seeds/animals
+ live-price liquidation value of held products
+ live-price value of empirically expected remaining crop/animal output
+ land at paid cost
```

The per-step state reward is only the increase above the episode's previous
high-water value. A decline emits zero, and returning to an already rewarded
state emits zero, so a buy/sell cycle cannot recollect the same achievement.
Valid WATER/FEED/CARE actions receive a small, live-price, data-valued credit;
duplicate hands on one tile are counted once. Terminal cash above the starting
bank receives an additional positive-only reward. To run this objective alone,
set `reward_potential_scale`, `reward_cash_scale`, and `reward_money_scale` to
zero and keep `train.reward_clip=0`.

The item coefficients are measured from exact-version elite replays by
`fit_elite_economic_values.py`. Aggregate seed/crop/animal/product/maintenance
scales remain ordinary INI values, and the fitter emits compact built-in sweep
ranges alongside the audit JSON. Unplanted seeds and unplaced animals are not
assigned speculative lifetime output, which prevents inventory hoarding from
looking like a productive farm. Current market prices enter the value directly,
so carrot, tomato, and egg become attractive only in their profitable games.
The audited six-day `1.32.7` fit used for the first experiment is preserved in
`elite_fits/1.32.7_2026-08-16_2026-08-21.{json,ini,sweep.ini}`.

### Restartable elite opponent factory

The factory downloads only exact `1.32.7` daily archives, rebuilds the merged
BC dataset, fits economic coefficients, and creates independent 128x2, 256x2,
and 512x2 clones:

```bash
KAG_CLONE_REFRESH=1 ./ocean/kaggriculture/elite_clone_factory.sh
```

Each clone contains one exact displayed agent name, a minimum number of elite
trajectories, and at least two source days. Since public replay JSON does not
provide a submission hash, the planner also compares per-head action-label
fingerprints across days. Days above `KAG_CLONE_MAX_BEHAVIOR_JSD` are excluded
instead of silently blending a changed bot. Ryo Hasegawa is mandatory and is
trained first. The auditable plan, per-player datasets, logs, fitted reward
files, and model manifest live under `/workspace/elite_replays/clone_factory`;
trained weights live under `saved/kaggriculture_elite_clones`.

```bash
./puffer train kaggriculture
```

### Staged sweeps

Do not sweep PPO hypers, opponent composition, and reward coefficients in one
search. First search training hypers from the current champion while rewards
and the 75%-rules/25%-league curriculum stay fixed:

```bash
./ocean/kaggriculture/sweep_v2.sh hypers
```

The default is 48 trials of 25M steps. Each invocation writes a raw log and a
ranked TSV containing every run's INI and final checkpoint. Tournament the top
eight against each other, established baselines, and native rules:

```bash
./ocean/kaggriculture/eval_sweep_v2.sh \
  logs/kaggriculture/v2_hypers_sweep_TIMESTAMP.tsv 8 50
```

Then pass the selected checkpoint and INI into the 40-run reward search:

```bash
./ocean/kaggriculture/sweep_v2.sh rewards \
  checkpoints/kaggriculture/SWEEP_RUN/FINAL.bin \
  logs/kaggriculture/SWEEP_RUN.ini
```

The first v2 hyper tournament selected run 31's optimizer settings while
retaining the unchanged champion weights. Its reward sweep command is:

```bash
./ocean/kaggriculture/sweep_v2.sh rewards \
  saved/kaggriculture_v2/champion_crop_real_opening.bin \
  saved/kaggriculture_v2/selected_hypers_20260803.ini
```

The crop reward phase searches potential magnitude, terminal win credit,
seed/product/crop valuation, neglect discount, and liquidation timing. Animal
and land reward values are deferred until policies use those systems reliably;
otherwise those dimensions contain no useful signal. Confirm the best three or
four configurations with 75–100M runs and multiple seeds before changing the
checked-in defaults.

The checked-in self-play pool is a cross-play-selected restricted population.
Its PSRO manifest reserves 70% of the solved meta-game mixture for confirmed
support, 25% for quality-gated diverse policies, and 5% for legacy exploration.
Those solved weights are evaluation metadata, not training probabilities.
Training uses variance PFSP from a uniform base across the restricted
population: competitive opponents near 50% learner score receive the most
weight, while a 10% uniform branch preserves every matchup. The frozen bank
rotates every one million steps. This prevents initially unbeatable policies
from starving reachable curriculum opponents.

`psro.sh` closes the population-management loop around training. One default
invocation finds the newest non-sweep run, samples 16 stages by training
percentage, evaluates them with the active league, solves the antisymmetric
zero-sum meta-game in native C, and measures two complementary novelty signals.
Payoff JSD compares each policy's win/draw signature against the common opponent
panel. Behavioral JSD forwards every checkpoint through the same observation
and action-mask sequences while preserving each model's recurrent state; heads
with fewer than two legal actions are excluded. Strong meta, economy, and
greedily diverse candidates are confirmed with 500 games per pairing. PSRO then
admits up to four, archives the previous league, records the role-weighted
mixture in the league manifest, and writes the selected pool back to the INI:

```bash
./ocean/kaggriculture/psro.sh
```

The default `iterate` path also selects the learner checkpoint automatically:
it chooses the highest confirmed meta weight among the new run's checkpoints,
then writes both the source and active-league paths to
`saved/kaggriculture_league_v3/learner.tsv` and updates `load_model_path`. This is
why the run ID does not need to be copied by hand. `--profile-games 0` disables
the final profile when a fast PSRO pass is wanted. Otherwise PSRO writes a
bench-only behavior report beside its logs, including animal buys/placements,
feed/care/fertilizer, coop/pasture construction, land orders, and market
subcategories:

```bash
./ocean/kaggriculture/profile_population.sh --games 50 \
  saved/kaggriculture_league
```

These are action-intent counters from the native evaluator, not privileged
training observations and not proof that an order succeeded. The existing
episode logs (`land_purchases`, `water_coverage`, `neglect_deaths`, and
`productive_extra_tiles`) remain the authoritative outcome metrics.

An existing expensive screen can be re-solved and diversified without replaying
all pairings:

```bash
./ocean/kaggriculture/psro.sh analyze --reuse logs/kaggriculture/psro_RUN_STEPS
./ocean/kaggriculture/psro.sh iterate --reuse logs/kaggriculture/psro_RUN_STEPS
```

The next `./puffer train kaggriculture` is the response-training phase of the
next iteration. PFSP remains enabled as an exploit-aware curriculum over the
PSRO-selected population, while the solved mixture remains available for
meta-game evaluation. The scripted economy-bot curriculum remains separate
because scripted policies cannot be loaded into a frozen CUDA bank. The default
split is 75% mixed economy bot and 25% learned opponent. Frozen-bank swaps split
evenly between the external PSRO population and checkpoints from the current
run.

The policy ABI currently has one compact market head. It can select every
market operation and quantity encoding, but it emits one market order per
turn; the rule core itself still accepts the official queue of up to ten. A
Python Kaggle trace may therefore show more orders per turn than a native
learner. Raising the native market-head count is an ABI/parameter change, not
a PSRO setting.

Experiment results, failed configurations, retention rules, and the warm-start
selfplay sweep protocol are recorded in `RESEARCH.md`.

Training uses 2,048 agents with a recurrent horizon of 16, producing a 32,768
transition rollout. Compared with the original 2,048 x 64 setup, the learner
refreshes its policy four times more often while retaining the large inference
batch needed for throughput. PPO uses one pass over each rollout: asynchronous
collection already makes the next rollout one update old, and replaying it four
times caused extreme KL divergence and poor fixed-opponent play. Entropy does
not anneal away. The CUDA sampler excludes frozen-opponent trajectories before
advantage normalization and PPO, so opponent actions cannot contaminate the
learner's policy, value, EMAg, or KL statistics.

The native population evaluator accepts checkpoints, one or more recursive
directories, or an evenly sampled long run. It runs 50 games per pairing by
default, splits seats evenly, and writes a manifest, directed payoff matrix,
ranking, fixed-bot results, and detected three-policy cycles as compact TSVs:

```bash
./ocean/kaggriculture/eval_population.sh
./ocean/kaggriculture/eval_population.sh --latest-run --range 50:100:16
./ocean/kaggriculture/eval_population.sh --final-only checkpoints/kaggriculture
```

Use `./ocean/kaggriculture/psro.sh runs` to list run checkpoint counts and step
ranges. `./ocean/kaggriculture/psro.sh analyze --range 40:100:20` performs the
same matrix and meta-game analysis without admitting or deleting anything.

Checkpoints from an older observation/action ABI are rejected by exact native
weight-shape validation instead of being run with misaligned parameters.

The INI exposes one neutral accounting-net-worth weight and one terminal-cash
weight. Individual seed, product, crop, animal, land, and action coefficients
were removed. A fixed-architecture screening sweep runs with:

```bash
./puffer sweep kaggriculture train.total_timesteps=20000000
```

## Public competitor code

The authenticated CLI can enumerate and pull public competition kernels for
strategy mining:

```bash
kaggle kernels list --competition kaggriculture --page-size 200 --format json
kaggle kernels pull OWNER/SLUG -p ocean/kaggriculture/reference/kernels
```

Inspect pulled code before using it as an opponent or copying a strategy.
The native `rules` opponent ranks crops continuously from live market prices,
unlocked-shop demand, remaining production events, its own acreage, and the
opponent's visible acreage. It diversifies across the top three opportunities,
keeps some wheat exposure, stops planting crops whose product price no longer
covers seed cost, expands land, independently schedules every hired hand, and
liquidates shed inventory. `carrot`, `melon`, and `wheat` use the same scheduler
with capped fixed-crop portfolios. The headed viewer and training curriculum
invoke these same structured bots, including all ten native market-order slots.
The Lux Design S3 repository is a different game and is not part of this
port.

## Kaggle submission

Kaggriculture submissions need a Kaggle-compatible `main.py` and model at the
archive root. `submission/main.py` mirrors the v2 observation, masks, MinGRU,
and 1/255 input normalization. The current crop champion package is
`pufferlib_kaggriculture_v2_crop_champion.tar.gz`. Competition baselines
downloaded from public notebooks are reference material only; they
are not submission candidates for this project.
