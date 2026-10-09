# SKILL001: FPG training and transfer, 2026-10-02

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

**Historical contract v1 results.** The 2026-10-03 audit found real simulator and
observation mismatches: camera boundaries, parser residency, offscreen/unknown
terrain, flag spawning, and omitted enemy interactions. The stronger gate and
fixes are recorded in [FIDELITY.md](FIDELITY.md). The numbers below are preserved
as historical measurements; they do not validate simulator fidelity. The active
v2 config starts fresh, and no v2 training has run.

More training substantially improved synthetic stair-start FPGs. A policy trained
only in simulation can execute final contact in the ROM, but **ROM approach and
stair starts remain unsolved. No pipe-exit-to-FPG policy has been established.**
The final checkpoint regresses on evaluation; the better evaluated midpoint is
the selected historical checkpoint, and all checkpoints remain available.

The original Mario Lab checkpoints and experiments remain separate.

## What ran

| Run | Initialization | Native frames / decisions | Reported training time |
| --- | --- | --- | --- |
| Qualification pilot `1790956507784` | Fresh H64/L2 MinGRU | 8,388,608 | 19.31 s |
| Continuous curriculum `1790957064845` | Fresh H64/L2 MinGRU | 67,108,864 | 164.62 s |
| Continuation `1790959626339` | Weights from the 67.1m checkpoint | 268,435,456 additional; 335,544,320 final lineage | 852.05 s |

All use terminal +1 for FPG, 0 otherwise, frameskip 1, twelve legal button
combinations, and original synthetic courses. There is no BC and no ROM PPO.
The 67.1m run retained the optimizer and adaptive sampler throughout. Its 8.4m
checkpoint is byte-identical to the pilot's final checkpoint; training then
continues through 67.1m in that same process. The continuation loads those learned
weights, restarts optimizer/recurrent/sampler state once, then retains them
throughout its additional 268.4m frames. Physics, reset bank, observations,
architecture, rewards, and hyperparameters are unchanged. Total compute across
the three runs is 343,932,928 frames, including the separate pilot.
All training has stopped.

The selected v1 checkpoint is
`checkpoints/mario_fpg/1790959626339/0000000134217728.bin`: **201,326,592 lineage
frames**, including its 67.1m parent. The final 335.5m checkpoint is
`checkpoints/mario_fpg/1790959626339/0000000268435456.bin`.
The latest native log is `logs/mario_fpg/1790959626339.ini`.
Configs, checkpoint/source hashes, exact-count evaluations, and ROM action tapes
are archived in [pilot_20261002](results/pilot_20261002/manifest.json) and
[continuous_20261002](results/continuous_20261002/manifest.json), plus
[continued_20261002](results/continued_20261002/manifest.json). The continuation
archive includes its native log and a CSV of rounded console training snapshots.
Its 128 native checkpoints remain saved; three were evaluated below.

## What learned and transferred

Greedy FPG successes on fixed evaluation panels before the continuation:

| Start tier | Pilot: synthetic validation / test | 67.1m: synthetic validation / test | Pilot: ROM | 67.1m: ROM |
| --- | --- | --- | --- | --- |
| Contact, last 1–8 reference frames | 32/64 · 40/64 | 38/64 · 51/64 | 6/8 distinct starts | 7/8 distinct starts |
| Approach, last 9–32 | 4/64 · 1/64 | 17/64 · 22/64 | 0/24 distinct starts | 0/24 distinct starts |
| Jump, up to last 96 | 0/64 · 0/64 | 12/64 · 5/64 | 0/32 sampled starts | 0/32 sampled starts |
| Whole stair setup | 0/64 · 0/64 | 0/64 · 0/64 | 0/1 start | 0/1 start |

During the continuation, greedy synthetic validation / test successes:

| Start tier | 134.2m total | 201.3m total, selected | 335.5m total, final |
| --- | --- | --- | --- |
| Contact | 43/64 · 54/64 | 45/64 · 54/64 | 44/64 · 55/64 |
| Approach | 20/64 · 29/64 | 24/64 · 32/64 | 20/64 · 28/64 |
| Jump | 21/64 · 14/64 | 41/64 · 30/64 | 39/64 · 28/64 |
| Whole stair setup | 15/64 · 0/64 | 60/64 · 56/64 | 60/64 · 51/64 |

The selected checkpoint has the highest summed greedy **validation** success
across the four equally weighted tiers among these three evaluations: 170/256,
versus 163/256 for the final checkpoint. ROM and test-split scores were not the
selection criterion. With sampled actions, selected-model stair success is
52/64 and 37/64; final-model stair success is 49/64 and 35/64.

Greedy ROM successes on the same reference starts:

| Start tier | 134.2m total | 201.3m total, selected | 335.5m total, final |
| --- | --- | --- | --- |
| Contact | 7/8 distinct starts | 6/8 | 3/8 |
| Approach | 0/24 distinct starts | 0/24 | 0/24 |
| Jump | 0/32 sampled starts | 0/32 | 0/32 |
| Whole stair setup | 0/1 start | 0/1 | 0/1 |

Sampled ROM contact success is 26/32, 24/32, and 17/32 respectively; all other
sampled tiers get zero at these three checkpoints. At the final checkpoint,
every greedy approach and stair trial ends in an ordinary flag grab. The new
synthetic skill has not become a reliable real FPG approach. More training alone
helped the synthetic task but did not resolve this transfer gap, and eventually
reduced close-contact transfer.

The ROM panel uses one naturally reached reference trajectory. The contact
evaluation repeats eight starts four times; approach repeats 24 starts over
32 trials; jump samples 32 starts spread over remaining distances 33–96; the
stair panel repeats one start. These are not independent routes or full levels.
Every action tape reproduces the complete final NES state on replay.

With sampled actions, the original 67.1m model gets 27/32 contact successes and 1/32
approach successes; jump and stair remain zero. The close-range controls get
7/32 for random actions, 8/32 for no input, and 4/32 for constant right+run.
The learned policy uses different inputs across the close starts; success is
not just the unavoidable continuation of every reset.

The synthetic seed panels are disjoint from training, but contain only eight
courses each, with multiple controller-phase/reset samples. They have now been
consulted during development and are not a sealed final benchmark.

## The sampler kept working

At the end of the pilot, extra practice still went to contact. In the longer
run it moved through approach and then to jump, while retaining the other tiers.
The last logged interval of that 67.1m run shows:

| Tier | Success | Fraction of sampled episodes |
| --- | --- | --- |
| Contact | 86.8% | 11.1% |
| Approach | 80.2% | 14.9% |
| Jump | 22.2% | 65.3% |
| Stair start | 0% | 8.7% |

The distribution changed without replacing the policy or restarting the
optimizer. The overall mixture reward fell as more difficult starts became
common, while the earlier skills improved. This is why a single aggregate reward
is a poor progress measure for an adaptive curriculum.

The continuation advanced from jump to stair practice. Its final logged interval:

| Tier | Success | Fraction of sampled episodes |
| --- | --- | --- |
| Contact | 83.6% | 9.6% |
| Approach | 86.7% | 10.3% |
| Jump | 92.0% | 10.8% |
| Stair start | 94.9% | 69.3% |

More training addressed the initial synthetic stair-start failure within these
course families. The remaining training/validation/ROM approach gap still
suggests inadequate coverage or policy generalization; the cause has not been
isolated from observation differences. More frames alone did not solve it. Reset
augmentation currently varies horizontal position/acceleration fractions only,
and accepts candidates when an existing successful suffix still works. That
filters the distribution toward the searched solutions.

The v1 observation adapter made unknown cells appear empty, exposed stale ring
buffer tiles at some parser phases, and padded rows differently from the
synthetic world. Those are implementation defects. Contract v2 corrects them;
the [fidelity report](FIDELITY.md) replaces the old observation qualification.
The fraction of policy failures caused by each defect has not been measured.

## Accuracy and speed

- 15,144 one-frame comparisons against the ROM across four reachable end-section
  trajectories: zero controller mismatches in the qualified small-player scope.
- A searched 100-frame sequence replays identically in the ROM: grab Y=164,
  flag stays at Y=48, end-of-level routine=5. The inputs are included with
  [provenance](reference/provenance.json). This is a local search result, not a
  perfect or full-game TAS.
- 1,536 augmented synthetic resets replay to FPG; ordinary low flag grabs are
  rejected. ASan/UBSan checks passed.
- 8,704 CPU/CUDA decisions, 524 resets, and 311 FPG successes agree, including
  CUDA graph replay and adaptive sampling.
- The 67.1m policy's contact, approach, and jump ROM trials have zero one-frame
  controller disagreements. One failed stochastic stair trial has two differences
  at X=2853, the left camera boundary. Camera clamping is outside this local core.
- The continuation's selected 201.3m and final 335.5m checkpoints have zero
  one-frame controller disagreements across their 256 ROM trials each. The
  earlier 134.2m checkpoint has eight differences in two failed sampled approach
  trials near the left camera boundary at X=3045. Every recorded action tape
  replays to the complete final NES state. These checks do not establish
  observation equivalence between the synthetic and ROM adapters.

On the GTX 1060, the environment-only benchmark processes 8,388,608 requested
frames in 1.341479 s: **6.25m frames/s** including observations, autoresets, and
reset-phase validation, excluding PPO and initial bank loading. This uses fixed
per-environment action IDs, not learned-policy actions. End-to-end PPO in the
67.1m run averages **0.408m frames/s**; the continuation averages **0.315m frames/s**
over 852.05 seconds. These are measurements of this small FPG
task, not a performance prediction for an entire Mario engine.

## Next experiments

These were the v1 follow-ups. The first priority became the stricter fidelity
gate; [FIDELITY.md](FIDELITY.md) records that work. More training must follow
qualification of the mechanics and observation contract being trained.

1. Audit observation agreement on identical local geometry, including unknown
   columns and below-screen cells. Preserve the current adapter as a baseline.
2. Expand incoming vertical phases, speeds, A-button history, and recovery
   trajectories. Search alternative suffixes after perturbations instead of only
   accepting perturbations that the old suffix already solves. Add more geometry
   and structural holdouts; keep the same learner within each long training run.
3. Add finer reset distances around the observed approach failure boundary.
   Compare the current sampler with one driven by fresh procedural validation
   contexts, retaining uniform rehearsal in both.
4. Extend the calibrated scope backward through the actual post-pipe route,
   including enemies, bricks, camera boundaries, and pipe state. Then evaluate
   several natural pipe-exit phases and finally complete 1-1 runs.

None of these results establishes a full-game or glitchless record. The broader
[continuation plan](../mario_lab/CONTINUING_TRAINING.md) keeps FPG separate from
that objective and describes how longer-run failures should feed the curriculum.
