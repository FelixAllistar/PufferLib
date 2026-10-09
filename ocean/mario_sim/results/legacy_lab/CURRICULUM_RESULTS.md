# Practice and adaptive training — 2026-10-01

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

The synthetic policy now clears gaps and pipe routes reliably on new seeds.
Walker collisions remain the weakest family. Authored practice resets, separate
short/full goals and a bounded adaptive coordinator are implemented. These
results concern the approximate synthetic engine; real SMB1 transfer is untested.

## What changed

Practice starts cover gap approaches, pipe entry, underground exit and a full
underground route. Starting position, subpixel offset, velocity and previous jump
state vary. These are authored states, not verified TAS snapshots. Full courses
remain in the training mixture. A separate goal switch compares changed resets
with changed resets plus a short task endpoint; practice deadlines are independent.

Full-course family weights can also vary. The coordinator evaluates complete
root-start episodes, increases sampling of failing families, chooses gap/pipe
practice weights and retains all candidate checkpoints. It selects by minimum
family clear rate and then macro clear rate, with a fixed round/frame budget.
Clear times are recorded but do not determine selection. Configuration and usage
are in the [README](README.md).

## Initial reset comparisons

All three first gap experiments continued from the same original mixed checkpoint
for 10,485,760 frames each. Training used length 48, difficulty 0 and seed 73.
The control used only ordinary gap-course starts. The short-goal variant used
80% gap practice resets with a 240-frame deadline; the reset-only variant used
80% practice resets with the original flag goal and a 2,400-frame deadline.

Evaluation always disabled practice and used length 64, difficulty 1, a 2,400-frame
cap, validation split 1, seed 901 and 32 completed episodes per family.

| Stage | Gaps | Walkers | Underground | Composite |
| --- | --- | --- | --- | --- |
| Original mixed policy | 0/32 | 18/32 | 16/32 | 27/32 |
| Gap root-start control | 15/32 | 19/32 | 19/32 | 23/32 |
| Gap short-goal practice | 15/32 | 17/32 | 14/32 | 28/32 |
| Gap reset-only practice | 18/32 | 19/32 | 19/32 | 22/32 |
| Wider gap stage | 30/32 | 11/32 | 7/32 | 20/32 |
| Pipe practice stage | 20/32 | 14/32 | 27/32 | 31/32 |
| Adaptive mixed round 1 | 31/32 | 22/32 | 32/32 | 31/32 |
| Adaptive mixed round 2 | 32/32 | 24/32 | 32/32 | 31/32 |

Flat, pipe-obstacle and stair families were 32/32 at every stage. The first matched
frame budgets show no advantage for the short-goal setup on this panel. The small
reset-only difference is preliminary: this is one training seed and 32 evaluation
episodes. The short-goal variant also changes the goal and deadline, so it does
not isolate the effect of resetting alone.

The wider stage added 20,971,520 frames to the root-start control, using length 64,
difficulty 2 and 50% short gap practice. It improved gaps while losing other
skills. Pipe practice then added 20,971,520 frames at difficulty 1, with 70%
practice probability, entry/exit/route weights 1/1/2 and an 800-frame practice cap.
Its quick CUDA route check was 100%, but the completed root-start panel was
27/32. Complete panels are the quality measurement.

Two adaptive mixed rounds added 20,971,520 frames each at length 64, difficulty 1,
35% practice probability and an 800-frame practice cap. Their training seeds were
74 and 75. Every full-course family retained positive sampling weight. The second
round won the declared validation selection rule; it was frozen before test
evaluation. These later stages have different budgets, difficulty and sampling,
so they establish a useful trained policy rather than a causal curriculum benefit.

## Frozen policy on unused seeds

After selection, both the original mixed checkpoint and the selected checkpoint
were evaluated on split 2: 128 completed episodes per family, seed 901, length 64,
difficulty 1, a 2,400-frame deadline and ordinary root starts. No subsequent
training or model selection used this test panel.

| Family | Original clears | Selected clears | Original mean clear frames | Selected mean clear frames |
| --- | --- | --- | --- | --- |
| Flat | 128/128 | 128/128 | 617.5 | 351.3 |
| Gaps | 12/128 | **128/128** | 610.4 | 366.8 |
| Pipe obstacles | 128/128 | 128/128 | 666.1 | 380.1 |
| Stairs | 128/128 | 128/128 | 692.5 | 385.3 |
| Walkers | **89/128** | 80/128 | 616.8 | 371.7 |
| Required underground route | 56/128 | **125/128** | 1,324.7 | 667.7 |
| Composite | 103/128 | **124/128** | 982.8 | 467.0 |

The selected policy cleared 841/896 episodes (93.9%), versus 644/896 (71.9%) for
the original. Walker clear rate fell from 69.5% to 62.5%, despite improving on the
development panel during mixed training. The remaining selected-policy failures
were 48 walker deaths, three underground timeouts and four composite deaths.
Successful clears became faster; their mean times exclude failures and cannot be
read as reliable full-game completion times.

These seeds vary the existing generator templates. Structural holdouts and
Mario-controller calibration remain separate gates. The used split-2 panel is
now reported evidence; future model selection needs a newly reserved seed bank.

## Correctness and speed

- CPU and AddressSanitizer/UndefinedBehaviorSanitizer checks pass: movement,
  collision, pipe timing, observations, replay, 800 generated layouts and 400
  practice setups.
- Nine CUDA parity cases cover 17 environments × 256 decisions each, including
  graph execution, every practice family, reset-only goals and weighted full-course
  sampling. State, reward, observation, terminal and log comparisons pass.
- With practice disabled and equal family weights, 32,768 decisions across 64
  cases match the previous controller's original state bytes, rewards and
  observations exactly. See [legacy_parity.json](results/curriculum_20261001/legacy_parity.json).
- Direct policy evaluation with a practice cap larger than the root cap passes
  under sanitizers. Action-tape storage accommodates either deadline.
- The eight reported final checkpoints each contain 100,928 finite FP32 weights.
  Rounded console loss values are finite. All **3,584** recorded evaluation
  episodes replay their complete final state exactly.

On the GTX 1060 3GB, the current engine benchmark runs 16,777,216 frames in
5.747 seconds: **2.92 million frames/s**. This includes scripted actions,
observations and resets, and excludes policy/PPO. The measured 4,096 frames per
environment exclude warmup. Reset and clear counts include the 64 warmup steps.
See [benchmark.json](results/curriculum_20261001/benchmark.json).

The adaptive rounds each train 20,971,520 frames in approximately 51–52 wall
seconds, about 405k–410k frames/s including startup. Seven new training runs total
115,343,360 frames in 322.2 trainer wall seconds, excluding evaluation and
implementation work. These are small-policy, single-device qualification runs.
The selected lineage has more training than the original; their final comparison
does not have equal training budgets.

## Saved artifacts and next steps

[training.json](results/curriculum_20261001/training.json) records all settings,
parent/checkpoint/core identities, timings, panels and the adaptive study. Local
run bundles retain captured sources, binaries, configs, checkpoints and native-frame
action tapes, including the failed legacy-config evaluation before its fix.

- [Adaptive study and selected model](../../logs/mario_lab/curricula/20261001T114927Z_061910c0/manifest.json)
- [Selected policy test panel](../../logs/mario_lab/curricula/20261001T114927Z_061910c0/training/GEN001_adaptive-02_20261001T115100Z_d911125a/evaluations/20261001T115403Z_390535f6/manifest.json)
- [Original policy test panel](../../logs/mario_lab/experiments/CUDA001_mixed-from-flat_20261001T091823Z_559054b8/evaluations/20261001T115404Z_3f8e3cfe/manifest.json)

Next, diagnose walker approach/stomp failures, calibrate the character controller
against reference trajectories, and freeze the real-1-1 semantic observation and
action adapter. No ROM training is needed for that transfer evaluation. TAS resets,
behavior cloning, visual pretraining, QD, interpretability and both Go-Explore
backends remain separate tracks in the [experiment register](../retro/EXPERIMENTS.md).
