# Pokemon personality search (experimental)

This is **reward-space CMA-ES + native PPO + an unstructured behavior archive**.
It is CMA-ME-inspired, not a PPGA, CMA-MAE, AURORA, or CMA-ME reproduction.
CMA-ES searches five shaping coefficients; PPO trains each candidate in the
existing game; quality comes exclusively from actual battle outcomes.

## Run

From the repository root with the normal Pokemon/Zig/CUDA dependencies installed:

```bash
python -m unittest discover -s ocean/pokemon/tests -p 'test_qd*.py' -v
# Keep the Kaggriculture ./puffer executable intact.
NATIVE_OUTPUT_NAME=build/pokemon/puffer_qd bash build.sh pokemon
bash build.sh pokemon --fast
make -C ocean/pokemon test
```

Pokemon uses CPU environments and GPU policy training: do not pass `--gpu`.
Both binaries need rebuilding. A versioned trainer handshake verifies that the
requested weights reached the new configuration hook; versioned evaluation
profiles prevent an old evaluator silently omitting the new measurements.

Use the existing exported native league and an **unrestricted learner** checkpoint
(not one of the fixed/required-team specialists). Replace the checkpoint below:

```bash
python ocean/pokemon/qd.py run \
  --trainer build/pokemon/puffer_qd \
  --native-league leagues/pokemon/named_roster/native_two_20260910.ini \
  --seed-model checkpoints/pokemon/YOUR_RUN/YOUR_STEP.bin \
  --out runs/pokemon_qd_v1 \
  --population 4 --generations 2 --train-steps 10000000 --games 64
```

Omit `--seed-model` for random initialization. This example requests **110 million
training steps total**, not ten million: `(1 + generations*(population+1))*steps`.
There is an initial win-only seed continuation and an extra matched win-only
control per generation. Native batch rounding, evaluation, and failed/retried
jobs add to actual compute. Each child has a fresh optimizer, not a full optimizer
resume of the parent.

For a separate wiring smoke test, use `--population 4 --generations 1
--train-steps 4194304 --games 8` and another output directory. With the checked-in
4096-agent configuration this requests six short PPO jobs. It is not a strength
test. The full normal engine tests should pass before this GPU smoke test.

Resume using the same arguments plus `--resume`; only `--generations` may increase.
The runner snapshots league checkpoints/configs, fingerprints inputs, records
CMA/RNG state, caches completed children, and retains failed-job logs. Changed
inputs or nonzero child exits fail loudly, rather than becoming fabricated
fitness scores. Checkpoints and their sidecars must not be edited during search.

## Personality signals

All five defaults are zero, preserving ordinary training. These are bounded
**state features**, not payouts for repeated move selection or guessed values:

| Weight | Own-side feature |
|---|---|
| `personality_paralysis` | Negative fraction of surviving members paralyzed. |
| `personality_sleep` | Negative fraction of surviving members asleep. |
| `personality_offense` | Positive active Attack, Speed and Special boosts, normalized. |
| `personality_defense` | Positive active Defense boost plus Light Screen/Reflect occupancy, normalized. |
| `personality_reserve` | Mean remaining HP fraction over the original six members. |

Sleep includes Rest: the observation does not identify its cause. These features
do not claim to count enemy-inflicted sleep, successful status moves, healing
moves, attacks, or "aggression." No species or moveset IDs enter these rewards.
Smogon strategy discussions motivate the feature selection, not numerical species
values. The existing sourced moveset catalog and expert-informed league remain
prior knowledge already present in the environment.

For signed coefficients `w=tanh(x)/max(1,sum(abs(tanh(x))))`, define
`Phi(s)=dot(w,f(own)-f(opponent))`. Add `gamma*Phi(next)-Phi(current)`, setting the
successor potential to **zero at every terminal**, including capped draws. Draft
features are zero. With matching learner/shaping discount and no reward clipping,
discounted shaping cancels across complete draft-and-battle episodes. The native
configuration hook synchronizes gamma and rejects clipping when shaping is on.

This changes finite-budget credit assignment and optimization paths, **not the
exact optimal discounted task objective**. It is not a permanent reward for a
particular personality. The outer search/archive supplies discovery pressure;
there is no guarantee that shaping alone will diversify a converged learner.

The training-only potential uses both players' own-state observations, including
full team HP/status, as the existing HP potential does. It does not expose those
private observations to the policy or use outside team ratings. Profiling uses
only the evaluated player's own trajectory. A future public-state-only shaping
ablation would be a different experiment.

`pokemon_base.h` preserves the original adapter byte-for-byte. The optional
`pokemon.h` wrapper leaves engine, masks, catalog and observation/action ABI
unchanged. It accounts for the original step's immediate terminal reset and
corrects episode-return logging. This hook targets native `./puffer`, not
`--slowly` or the Python learner path.

## Search, selection and controls

CMA-ES adapts a full 5x5 covariance. Each generation branches equal-budget PPO
children from one parent, then scores them against the same immutable league
panel, seeds and balanced seats. The extra zero-weight control uses that same
parent, seed and budget. CMA ranks new behavior regions first, then within-region
match-score improvement. A separate best-panel-score checkpoint is always kept.

Descriptors use realized species inclusion, **pair co-occurrence, whole moveset
teams**, leads, and averaged state features. Thus marginal "top six" usage does
not hide whether a combination ever appeared together. The archive has no
predefined grid cells, but its features, block weights and radius remain explicit
design choices; this is not learned AURORA geometry. A split-half noise allowance
reduces false novelty, without claiming a statistical confidence guarantee.

Weaker novel candidates can remain stepping stones. After three generations on
a parent (configurable), or no archive improvement, select the champion or an
archived parent and restart CMA. Changing parents resets covariance because it
changes the coefficient-to-policy mapping. At capacity, only local improvements
are admitted; increase capacity for a longer discovery run. These archive and
restart rules are custom heuristics, not claimed properties of the CMA-ME paper.

Runner overrides, applied equally to all candidates and controls:

- Native fixed league on, rolling-history self-play off, unrestricted team choice.
- Win/loss task reward, old HP/KO shaping off, no reward clipping.
- Horizon 512 and GAE lambda .995, versus checked-in 128/.98. This is a credit
  propagation experiment, not evidence of a broken draft gradient.
- Fixed learning-rate/entropy schedules within each continuation. Actual rate
  and entropy coefficient otherwise come from `config/pokemon.ini`.
- EMAg off by default. `--emag` can use the existing implementation; this patch
  neither replaces nor certifies it. Epoch sampling on, priority sampling off.

Reward-space search is substantially smaller than a PPGA port: genuine PPGA
requires multi-objective policy-gradient/directional-search machinery. This
experiment retains the existing scalar-reward native PPO backend.

## Evaluation-only oracle and limitations

The environment chooses six members using twelve private species/variant picks,
then battles in the same episode. A six-species legal team is not necessarily a
well-piloted team. Equal PPO adaptation budgets reduce, but do not eliminate,
the confounding between team quality and pilot competence.

The `report` command is never called by the search. For one candidate directory:

```bash
python ocean/pokemon/qd.py report \
  runs/pokemon_qd_v1/candidates/g0001_c000/evaluation \
  --core Chansey,Tauros,Snorlax
```

This reports joint inclusion of all three and the corresponding match score;
these names never affect rewards, CMA, archive admission or parent selection.
The champion path is printed and recorded in `state.json`. Evaluate it separately:

```bash
./pokemon eval /actual/champion.bin /actual/heldout.bin \
  --games=1024 --seed=314159 --profile-json \
  --team-a=None env.team_selection=1 env.max_updates=512
```

Reserve unseen opponents/seeds. Sixty-four games per bank are a search screen,
not reliable proof of improvement after selecting among many children. A common
panel average is not exploitability or equilibrium; this is not PSRO. Report
W/D/L and timeout rate, not only core inclusion. The sourced 556-set catalog and
512-update adjudicated draws make this different from unrestricted tournament
OU. High core inclusion is an oracle diagnostic, not a necessary/sufficient
condition for best play or a promise that the search will discover that core.

## Validation

Twenty-one CPU tests passed during implementation: CMA convergence/covariance/RNG
resume; descriptor, archive, and noise contracts; compiled C potential math;
entry-header build contract; compiled wrapper/reset bookkeeping with a mock base adapter; additive C profile
JSON with a mock engine; and a mocked full runner/resume/input-integrity test.
Mocks are clearly labeled and confined to tests. The production runner has no
synthetic-fitness fallback.

**Full native Pokemon/Zig builds, CUDA training, battle parity, and playing-strength
experiments have not been executed in the authoring environment.** Run the above
build, engine tests and wiring smoke test before committing a long GPU budget.

## Sources

- Smogon RBY OU viability discussion (evaluation context):
  https://www.smogon.com/forums/threads/rby-ou-viability-rankings.3685861/
- Smogon RBY battling guide (qualitative feature inspiration):
  https://www.smogon.com/rb/articles/rby_battling
- Hansen, CMA-ES tutorial: https://arxiv.org/abs/1604.00772
- Fontaine et al., CMA-ME: https://arxiv.org/abs/1912.02400
- Batra et al., PPGA: https://arxiv.org/abs/2305.13795
