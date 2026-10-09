# Continuing training without throwing away the curriculum

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

The long empty generated courses were not the intended next step. Extending the
map length did not give us a complete Mario curriculum. GEN002 remains an optional
generator ablation; it is not the main path to full-game performance. Its native
500m config was a proposed budget, not completed training. Only 1.05m and 4.19m
smoke runs were performed. The selected short-policy lineage accumulated 167.77m
synthetic frames; its frozen real 1-1 evaluation cleared 7/64 trials.

The next active experiment is the independent [FPG task](../mario_fpg/README.md),
from fresh weights as requested. Its precision controller, observations, and
action space differ from Mario Lab. Preserve the short-policy checkpoint for
separate continuation; it is not a compatible FPG initialization.

## What a continuing curriculum should do

A curriculum is a persistent distribution over practice, not a course taken once
and discarded. Within a compatible experiment, keep the same weights and
optimizer while changing that distribution. A new random seed/layout is a new
episode, not a reason to initialize a new model.

Use short tasks with genuine terminal success. Expand their reset distance as
success improves. Keep easier tasks in the distribution so learned skills do not
disappear. Once skills work individually, join them into longer, tested sequences
with useful approach, transition, and recovery space. Extra empty distance is
not a substitute for temporal credit assignment.

A starting mixture for the eventual broad simulator could be:

| Share | Purpose |
| --- | --- |
| 50% | Weakest currently learnable skill/context and difficult transitions |
| 20% | Longer supported sequences, including ordinary level starts |
| 20% | Uniform rehearsal across implemented skills and level families |
| 10% | New seeds, recovery states, and wider challenge variations |

These numbers are proposed knobs, not an established optimum or the current
FPG config. Keep lower bounds on every implemented family. Cap time spent on
unlearnable/impossible tasks. Prefer tasks showing learning progress or near the
mastery boundary; pure “most failures wins” can spend the entire budget on broken
physics, invalid resets, or impossible jumps.

The current FPG implementation is the small first version: 40% uniform retention
over four reset distances, 60% earliest weak tier, online estimates, one learner,
terminal-only reward, and replay-verified reset augmentation. It does not yet
diagnose failures in whole levels or schedule water/castle/enemy families.
The [first 67.1m-frame run](../mario_fpg/RESULTS.md) advanced from contact to
approach to jump practice without restarting. Its held-out and ROM approach
results also show why training-set mastery alone cannot determine promotion
to a claim of generalization.
The later continuation reached 335.5m total frames. Its selected 201.3m checkpoint
gets 60/64 and 56/64 greedy synthetic stair-start successes, but no ROM approach
or stair-start successes. The final checkpoint regresses on evaluation. All
checkpoints are preserved. A later audit found simulator/observation defects;
the active FPG v2 config now starts fresh, with training deferred until the
[ROM/native fidelity gate](../mario_fpg/FIDELITY.md) passes. Low visual fidelity
must retain the game's state transitions and observation semantics.

## Use full real runs as diagnosis

We can already run frozen policies on complete real levels. A synthetic long map
is not a real Mario full run. Continue ordinary learning in CUDA; periodically
evaluate in the ROM, then feed observed failures back into synthetic task design.
ROM Go-Explore remains a separate explicitly permitted experiment.

For each failure, preserve the seed/snapshot, preceding inputs, local geometry,
velocity/subpixels, previous buttons, nearby actors, and time remaining. Group
repeatable contexts: late takeoff, wrong release duration, enemy timing, pipe
alignment, landing/recovery, unknown tile/entity, or transition-state mismatch.
A nearby obstacle is not automatically the cause of death. Replay a short window
with action alternatives and compare sim/ROM state before labeling it.

Then distinguish three outcomes:

1. **The mechanics disagree.** Fix/calibrate the simulator and invalidate affected
   claims. More PPO on incorrect collisions will not solve transfer.
2. **The sim agrees but the policy cannot solve that context.** Add a local task,
   find valid successful starts, sample backward through them, and increase its
   practice weight while retaining the other tasks.
3. **The isolated task works but the sequence fails.** Practice the preceding
   transition, incoming speed, controller history, and recurrent state. Gradually
   include more of the prefix, then return it to the longer-run mixture.

The diagnostic ROM run is evaluation, not a new source of dense shaping reward.
Map a failure into original synthetic variations; keep a held-out set of real
start phases/routes for final comparisons. Do not repeatedly tune on the only
test bank and call it generalization.

## Terminal-only learning and longer horizons

Terminal-only reward is practical when the reset distribution makes success
discoverable. A last-five-frames reset can teach a precise action; moving the
start backward teaches how to arrive there. Search/expert trajectories can supply
valid starts without supplying supervised action labels. BC, action masking,
potential shaping, and frontier rewards remain separate ablations.

Use an explicit success predicate for each task and report it separately. A
successful gap crossing, ordinary level finish, and FPG are different outcomes.
Do not infer overall performance from a rising mixture-average reward as easy
resets become more frequent.

Frameskip 1 provides much finer control, but makes old step counts ambiguous.
167m one-frame decisions are 167m game frames; 167m four-frame decisions would
cover roughly 668m frames. Compare both game frames and decisions, plus wall time,
clear rate, and completion time. The earlier behavior is encouraging; it does
not establish that any particular larger budget will fix the remaining failures.

Discounting must reflect the new time unit. To preserve a discount defined per
four-frame action, the corresponding one-frame gamma is `gamma4 ** 0.25`.
Rollout length, GAE, recurrent unroll length, and start-distance distribution also
affect whether terminal outcomes can train the approach. The FPG pilot uses
gamma 0.995 and short tasks; broad full levels need a separate horizon study.

## What “maximum training” would mean

There is no universal final curriculum epoch followed by unrestricted RNG.
Keep the sampler active throughout the compute budget. Increase the share and
length of complete sequences when fixed evaluation panels show mastery, then
move practice back to failures when performance regresses. Add new level families
only after their mechanics and success conditions have tests and a reachable
baseline. Water, moving platforms, castle/boss, maze logic, and full-game state
transitions are still future implementations.

Use fixed per-family validation panels, longer-chain panels, and periodic real
level/full-game trials. Report success and finish-time distributions per family,
not just best episodes. Promote checkpoints based on these panels; maintain a
held-out final panel. A plateau triggers diagnosis or a new ablation, not an
automatic fresh model or an assumption that a larger map is better.

For durable continuation, eventually checkpoint optimizer, curriculum statistics,
sampler RNG, and environment/recurrent state alongside weights. Today's native
trainer saves weights and run config/history; it does **not** provide exact
training continuation. The FPG sampler persists during a run and restarts on a
new process. The older Python curriculum restarted the optimizer every round;
that is an experimental limitation, not the intended permanent architecture.

## Experiments still separate

No YouTube pretraining, BC, full TAS generation, Go-Explore, QD, or neuron probing
was performed by the earlier Mario Lab runs. SKILL001 now adds a local searched
FPG reference and a synthetic successful-reset bank. Neither is a complete-game
TAS corpus. Keep those future experiments in the [register](../retro/EXPERIMENTS.md)
with independent configs, checkpoints, data provenance, and evaluation panels.
