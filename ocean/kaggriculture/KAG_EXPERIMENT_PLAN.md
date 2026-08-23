# Kaggriculture experiment DAG

This is the execution graph for repairing the GPU training path and improving
the standing champion. A branch is promoted only from a both-seat evaluation
against the same native `top` opponent and the current champion.

## Historical baseline (superseded by B4 below)

- Champion: `saved/kaggriculture_hall_of_fame/expL_anim_top_73m.bin`
- Architecture: 128 hidden, 2 MinGRU layers
- Historical fixed-eval result: 9,005 money versus `top`
- Fresh CPU diagnostic (seed 42, 100 games/seat): $9,005 as player 0 and
  $7,254 as player 1; conservative both-seat mean $8,130
- Kaggle public score: 600.0

## Gates

1. Correctness gates must pass before training starts:
   - CPU/CUDA reward parity with nonzero terminal margin and inactivity knobs.
   - CPU/CUDA state, observation, mask, reward, terminal, and reset parity.
   - BC held-out active-head accuracy and closed-loop opening replay.
2. Screening uses at least 100 games per seat against `top` and ExpL.
3. A candidate is a champion only if it improves conservative both-seat
   money against `top` without a catastrophic head-to-head regression.
4. Every experiment records its parent, exact INI, checkpoint, seed, result,
   and conclusion in `RESEARCH.md`.

## Graph

```text
P0 preserve remote artifacts
  -> P1 repair CUDA reward parity
      -> D0 scripted top opening -> ExpL handoff diagnostic
      -> B0 repair opening BC pipeline
          -> B1 ExpL + recurrent opening BC patch
              -> R0 root/reset mixed PPO, no EMAg
              -> C1 first corrected champion
                  -> E0 matched no-EMAg / EMAg A/B
                  -> B3 BC dose/basin refinement
                      -> C2 second corrected champion
```

## Node definitions

### P0: preserve and identify artifacts

Copy the Vast hall of fame and experiment INIs locally. Record hashes before
changing code. Do not rely on the instance filesystem as persistent storage.

### P1: reward parity

Make CUDA apply the same terminal relative-money margin and configured
inactivity threshold as CPU. Add targeted cases that would fail if either
knob were ignored. Re-run the existing randomized adapter parity suite.

### D0: opening bottleneck diagnostic

Evaluate the unchanged ExpL weights in two modes:

- normal root control;
- the native top opening controls both model seats through turn 26, then ExpL
  takes over.

Run both seat assignments against `top`. This is diagnostic scripting, not a
trained policy. A material gain proves that root opening quality is a primary
bottleneck.

Result (2026-08-11): **rejected**. Exact native top actions through turn 26
gave $9,095 as player 0 but only $2,010 as player 1, a $5,553 both-seat mean.
The unmodified ExpL control scored $9,005/$7,254 ($8,130 mean) under the same
100-game-per-seat diagnostic. The opening alone is not the bottleneck: ExpL's
continuation is incompatible with the scripted farm/economy state. BC remains
a controlled low-weight branch, not the presumed mainline.

### B0: repair BC

- Honor `bc.steps`; opening datasets stop at the requested prefix.
- Preserve episode boundaries and train contiguous recurrent sequences.
- Compute imitation loss only for conditionally reached heads.
- Support initializing from an existing PufferNet checkpoint.
- Split train/validation games.
- Report active-head accuracy, exact executed-action accuracy, and closed-loop
  opening divergence on held-out seeds.

### B1: supervised opening patch

Initialize from ExpL and imitate top opening sequences through turn 26. Keep
later competence with either a small update budget or frozen ExpL distillation
on mid/late states. Do not use EMAg as a substitute for a frozen teacher.

Result: opening-only BC is rejected, but the repaired trainer exposed the
successful branch B2: imitate complete 719-decision top trajectories. The
25-epoch ExpL warm start reached $9,226 versus top over 500 games, versus
ExpL's $8,049 (+14.6%), and stayed effectively tied head-to-head (49.6%).
Training longer was harmful ($8,553 at 50 epochs; $6,874 at 100). This model
is promoted as C1: `expR_fullbc_top_e25.bin`.

### R0: DAGS-style reset mixture

Use an explicit `reset_opening_prob` independent of the sampled prefix range.
Initial branch:

- 50% genuine turn-zero episodes;
- 50% top-opening reset states sampled from turns 10 through 80;
- one observation bit identifying root versus reset source;
- full 720-turn episodes;
- 100% native `top` opponent;
- no EMAg.

The reset branch teaches continuation. Root episodes plus BC teach the actual
opening. Early episodes are not truncated as terminal games.

Result (2026-08-11): **small transient gain, not promoted**. Root-only
screening put the 5.24M checkpoint at $8,523 versus the sanitized ExpL's
$7,973 on 100 common-seed games, but all later checkpoints decayed. A
500-game confirmation reduced the advantage to $8,134 versus $8,049, while
the candidate lost the direct matchup 48%-52%. The explicit mixture works,
but the current update/reward recipe is not stable enough to clear the
champion gate.

### E0: controlled EMAg comparison

Start two runs from the exact same C1 checkpoint with identical seed, data,
opponents, and PPO hyperparameters. The only difference is EMAg coefficient
and tau. The conditional objective was source-audited before the comparison:
it computes `KL(magnet || policy)`, uses the `p - q` logit gradient, and weights
each child by its probability of being reached under the magnet rather than by
the sampled rollout path. The BC teacher, if used, remains frozen and separate
from EMAg.

Result: the moving EMAg reference stabilized R0's later checkpoints but did
not improve the confirmed policy. At 500 fixed-top games, ExpL scored $8,049,
R0@5.24M $8,134, and EMAg@20.97M $7,991; EMAg ranked last in their direct
matrix. Moving EMAg is not promoted. A second run used C1 as an actually
frozen (`emag_tau=0`) conditional KL reference during root PPO. Its screened
10.49M checkpoint scored $9,088 over 500 fixed-top games versus C1's $9,226
and tied C1 exactly head-to-head, so frozen-reference PPO is also rejected.

### B3: BC dose and basin refinement

The full-trajectory BC peak was refined without changing architecture, data,
initial policy, or optimizer. A 15/20/25/30/35 epoch curve confirmed that 25
epochs is the closed-loop optimum even though held-out imitation accuracy
continues increasing. Twelve independent episode split/shuffle seeds were
then trained at that fixed dose.

Seed 303 cleared two independent fixed-top confirmations. In the final
2,000-game block it earned $9,345 versus C1's $9,208. Their direct match was
statistically tied (49.95% C2 wins), but C2 earned $13,103 versus $13,045;
combined with the preceding 1,000-game block, C2's direct win rate is 50.43%.
It is promoted as the stronger top-specialist while C1 remains preserved:

`saved/kaggriculture_hall_of_fame/expS_fullbc_seed303_e25.bin`

Fixed-opponent evaluation now uses common random numbers for every candidate,
with `KAG_FIXED_SEED_A/B` overrides for genuinely fresh confirmation blocks.
Previously, `eval_population.sh` varied the world seed by policy index, which
added avoidable ranking noise to close promotion decisions.

### P2: top-opponent oracle parity repair

Closeout testing found that the Python top export and native C opponent
diverged at turn 26. The Python continuation schedules fertilizer collection,
animal-product harvest, feeding, and care before crop jobs; native C omitted
that entire pass. The native planner now uses the identical row-major animal
job order and tie-breaking. Full 719-decision parity passes for seeds 7, 42,
and 707.

The prior fixed-top dollar figures describe the pre-repair opponent and are
retained as experiment history, not current benchmark values. A fresh common-
seed 1,000-game panel against the oracle-exact opponent gives:

| policy | money | win rate |
|---|---:|---:|
| ExpL | 20,121 | 89.0% |
| C1 | 21,593 | 98.6% |
| C2 | **21,814** | **98.7%** |

C2 also beats C1 directly 53.5%, earning $13,311 versus $12,749. Thus the
second promotion remains valid—and becomes much clearer—after oracle repair.
A regenerated animal-oracle dataset was tested at 1/3/5/10 BC epochs from C2.
Every dose was harmful; one epoch already fell to $21,587 and lost C2 44%-56%,
with further doses degrading monotonically. The animal oracle remains an
opponent, not an imitation target.

This game remains non-transitive: ExpL narrowly beats C2 directly (51.6%) even
though C2 is dramatically stronger against the target top bot and beats C1.
Keep ExpL, C1, and C2 rather than replacing the hall of fame with one file.
The Kaggle-ready C2 archive is
`ocean/kaggriculture/submission/pufferlib_expS_fullbc_seed303_e25.tar.gz`.

## Rejected shortcuts

- One-action opening masks do not teach the opening policy.
- Reset-only training does not teach actions before the reset state.
- `magnet_path` is only EMAg initialization; it is not a frozen BC anchor.
- Productive-action bonuses invite action/order spam.
- Short terminal games change the objective unless treated as bootstrapped
  rollout cuts rather than environment terminations.

## B4: faithful compact-action BC and prefix-frozen KL

Later audit invalidated the C1/C2 promotion claim. The recurrent BC generator
had labeled compact actions while stepping richer teacher actions, and native
screening sampled rather than evaluating the deterministic exported policy.
The Kaggle result (~405 for C2) is consistent with that failure.

The repaired graph is:

```text
rich top action -> compact heads -> decode representable action -> step
  -> 26-step recurrent BC -> deterministic opening gate
  -> 96-step perturbed recovery BC -> deterministic recovery gate
  -> PPO from clone + frozen KL only through t~=96
  -> diverse bot and learned-policy opponents
```

Authoritative artifacts:

- `saved/kaggriculture_bc_v2/opening_anchor.bin`: four-animal opening in both
  seats.
- `saved/kaggriculture_bc_v2/top_clone.bin`: four animals and 9+ feeds through
  96 decisions in both seats.
- `ocean/kaggriculture/bc_pipeline.sh`: regenerates both datasets/models.
- `ocean/kaggriculture/train_bc_ppo.sh`: no strategic mask/reset curriculum
  and no per-action/inactivity/neglect bonus.
- `ocean/kaggriculture/package_model.sh`: deterministic full-game package
  smoke before producing a Kaggle archive.

Do not rank a learned policy as champion from sampled dashboard scores.
Promotion requires deterministic both-seat fixed panels, head-to-head games,
and successful package gates.

## S0: replay-state reset bank and reverse economic curriculum

Goal: teach reusable economic operations before asking PPO to discover an
entire 720-turn strategy. The desired abstraction is conditional value
maximization (buy, hold, maintain, invest, and sell when their expected value
is favorable), not a memorized product route.

### State sources and validity

1. Reconstruct official elite episodes by resetting the parity-tested native
   simulator and stepping both recorded action streams. Save complete native
   `KGState` snapshots, never observation bytes alone. The latter omit state
   required to continue a valid game.
2. Verify every reconstructed snapshot against the corresponding official
   observations for both seats before admitting it to the state bank.
3. Retain the recorded next action, player/agent identity, module version,
   final money, winner, turn, prices, inventory, remaining time, production,
   maintenance, store, and opportunity metadata.
4. Add native bot/clone/policy rollouts for recovery coverage. Elite states
   remain the quality anchor; synthetic states are restricted to explicit
   counterfactual tests or mutations whose invariants are checked.
5. Split by complete episode and agent lineage. Never place turns from one
   episode in both training and validation.

### Scenario taxonomy

Index snapshots into overlapping, product-balanced classes:

- `sell_now`, `hold_for_later`, and `buy_opportunity`;
- `carrot_opportunity`, `tomato_opportunity`, and `egg_opportunity`;
- `liquidation_1d`, `liquidation_3d`, and `liquidation_6d`;
- `maintenance_profitable`, `maintenance_unprofitable`, and `harvest_ready`;
- `short_investment`, `medium_investment`, and `early_expansion`;
- `recovery` for neglected assets, weeds, stranded inventory, and damaged
  openings.

The replay indexer's observable opportunity tag is based on live price versus
the product's equilibrium price. Once native reconstruction is available, the
authoritative carrot/tomato/egg demand tag uses the simulator's hidden
`exogenous_demand_units`, matching the environment metric.

### Reverse curriculum

```text
immediate sell
  -> conditional buy/sell
  -> delayed store/price opportunity
  -> maintain and harvest existing assets
  -> short-horizon planting/animal placement
  -> progressively earlier investment and expansion
  -> genuine turn-zero games against the diverse league
```

Use one economic objective throughout. A curriculum reset is a continuing
state, not an artificial terminal objective. Promote between stages with a
fixed scenario gate rather than elapsed training steps. When adding a stage,
retain 20--30% of earlier states to prevent selling/maintenance forgetting.
The final stage preserves a nonzero root-start fraction and gradually anneals
the reset fraction rather than switching distributions abruptly.

### Counterfactual and interpretability gates

Build paired states that differ in one controlled fact: one price/demand,
store availability, remaining days, inventory quantity, product identity, or
maintenance status. For stochastic and deterministic policies report:

- pre-mask and post-mask action probabilities;
- action/logit deltas under each counterfactual;
- observation-group occlusion effects;
- hidden-state probes for price, inventory, time, profitability, and urgency;
- recurrent retention after a visible opportunity signal disappears;
- activation-patching effects between matched high- and low-value states.

Compare at least an elite clone, crop specialist, animal specialist, current
champion, and current reward-sweep winner. This separates missing perception,
missing recurrent memory, decoder/action-mask failure, and credit assignment.

### Execution phases

- **S0a (sweep-safe):** specify the versioned index/state formats; implement a
  streaming, read-only replay classifier; validate it on small fixtures.
- **S0b (implemented locally):** replay actions through the native core,
  serialize complete states, and enforce two-seat parity/invariant checks.
- **S0c:** add a GPU reset-bank sampler and configurable root/stage mixture.
- **S0d:** add the scenario evaluator and causal probe reports.
- **S0e:** run the reverse curriculum, then confirm on untouched root games,
  deterministic/stochastic fixed panels, PSRO, and Kaggle packaging gates.

While an unrelated GPU sweep is active, S0a may run locally. Do not rebuild the
remote binary, mutate its active config, launch GPU probes, or perform the full
multi-gigabyte remote extraction until that sweep finishes.

S0a implementation: `index_replay_states.py` streams JSON, JSON.GZ, directory,
or ZIP replay inputs into a TSV locator index and adjacent audit JSON. Example:

```bash
python3 ocean/kaggriculture/index_replay_states.py \
  '/workspace/elite_replays/raw/*.zip' \
  --output /workspace/elite_replays/state_bank/replay_index.tsv \
  --only-scenarios --min-version 1.32.7
```

The v1 index deliberately contains no raw `KGState` bytes and prints that
warning in its audit. Its rows identify verified source episode/turn/player
locations for S0b reconstruction.

S0b implementation: `build_replay_state_bank.py` consumes that index and the
same replay inputs. It replays both recorded seats, parity-checks every native
frame traversed, serializes one complete native state per selected
episode/turn, then deserializes and advances a second copy through the next
recorded joint action. A state is admitted only if both the restored frame and
the resumed next frame match the official replay. Example (run after the
active sweep, on the machine holding the replay archive):

```bash
python3 ocean/kaggriculture/build_replay_state_bank.py \
  '/workspace/elite_replays/raw/*.zip' \
  --index /workspace/elite_replays/state_bank/replay_index.tsv \
  --output /workspace/elite_replays/state_bank/native_states.kgb \
  --lib ocean/kaggriculture/build/libkaggriculture.so \
  --min-version 1.32.7
```

The binary header records the bank format version, native serialization
version, exact `KGState` byte size, and record count. The adjacent manifest
retains all S0a rows, both expert actions, offsets, and SHA-256 hashes. Raw
state bytes are deliberately ABI-bound: a loader must reject a version or
size mismatch. `make -C ocean/kaggriculture state-bank-test` exercises a real
official replay and proves frame parity plus serialize/deserialize/resume.
