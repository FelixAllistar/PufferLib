# Fidelity reset — 2026-10-03

**Historical learning conclusions remain withdrawn.** The prior approximate simulator did not
produce the training states we intended. Its learning results cannot establish
which encoder, reward, curriculum, reset distribution, or amount of training is
appropriate for the real game.

The follow-on optimized runtime now passes the archived reconstruction panel,
generated-world replays and environment integration checks. Fresh integration
training has begun under the new raw-RAM contract; performance and artifacts are
recorded separately from the feasibility archive. This permits bounded runtime
work without claiming exhaustive coverage of every game interaction, arbitrary
procedural geometry, or a validated encoder/reward/curriculum choice.

The raw measurements, checkpoints, configurations and replay inputs remain
preserved. A recorded ROM clear is still a recorded ROM clear. It does not
validate the synthetic dynamics, identify the cause of a failure, or justify an
architecture or reward choice. Corrected-simulator experiments must be new runs
with an explicit observation contract and provenance.

| Claim or decision | Current evidential status | What is needed before relying on it |
| --- | --- | --- |
| Mario Lab's generated courses reproduce Mario training states | Invalidated: the controller, collisions, entities and adapter had material differences | Persistent state and observation parity for each included mechanic |
| More varied/longer courses improve or harm real-game learning | Unvalidated; simulator errors and distribution changes confound the comparisons | Matched ablations after fidelity qualification |
| Sparse reward works poorly, or shaping is necessary | Unvalidated; task dynamics and reachable successful states were wrong or insufficiently tested | New reward comparisons with qualified dynamics and verified task reachability |
| Sparse reward is sufficient for the intended real-game task | Unvalidated; success on a restricted synthetic reset bank does not establish this | Qualified reset bank and independent ROM evaluation |
| MLP, tile CNN, entity encoder, hidden size, or recurrent depth is the right choice | Unvalidated; no architecture conclusion survives the changed state/observation contract | Controlled encoder ablations under a fixed, qualified contract |
| The agent needs more frames or forgot a real-game skill | Unvalidated as a causal explanation | Matched corrected-simulator runs and preserved evaluation tapes |
| Mario Lab synthetic scores and timings | Historical measurements of that implementation only | Preserve the exact config/binary; do not relabel as corrected-game results |
| Recorded real-ROM policy trajectories and clears | Observed outcomes of the frozen policies and input protocol | Preserve seeds, action tapes, ROM identity and recurrent initialization |
| FPG contract-v1 learning conclusions | Unvalidated; v1 fails the stronger ROM parity gate | New runs after the appropriate scope passes qualification |
| FPG contract-v2 mechanics | Qualified only for the documented final 1-1 panel; CUDA float observations previously used a tolerance | Keep that regression; require bitwise observations for the new broad contract |
| The 448-value FPG observation can represent the full game | Not established; it omits most actors, powerups, transitions and game modes | Explicit full-mechanics observation coverage and aliasing tests |
| The entire game can be reconstructed by the existing randomization knobs | Not established; timers, PRNG, spawn/parser state, actor slots and several controllers are missing | Scene import and procedural generation must use the same configuration/state schema |
| The expanded engine is fast enough for useful PPO | Environment and fresh integration throughput now measured; learning efficiency remains unestablished | Keep environment and end-to-end PPO measurements separate; evaluate new learning runs independently |

This reset applies to CUDA001, OBS001, GEN001, GEN002, PHY001, SKILL001, and any
downstream proposal whose rationale came from their synthetic learning results.
Planned experiments are hypotheses, not recommendations supported by those runs.
Independent emulator correctness tests and measured implementation timings retain
their original, bounded meaning.

## Gate for new work

1. Inventory every distinct controller, interaction, spawn mechanism and transition
   in the original game, including hard-mode variants and hidden content.
2. Capture reachable ROM clips, their initial hidden state, action inputs and
   provenance. Label constructed interventions separately from reachable clips.
3. Reconstruct a scene once through the same parameters available to the generator.
   Persist the native state thereafter; do not overwrite it with reference state,
   reference positions, future spawn events or recorded trajectories.
4. Compare every frame's modeled state, outcome and complete observation exactly.
   Run the same persistent replay on CPU and CUDA. A missing mechanic, unsupported
   mode, tolerance-based match, or silently skipped failing clip cannot count as a
   full-scope pass.
5. Exercise collisions and interactions as well as unobstructed actor motion;
   cross actor phases, player forms, inputs, camera positions and terrain. Keep
   failures and publish coverage boundaries alongside passing counts.
6. Once the intended training scope passes, revisit observation/encoder design,
   reward, curriculum and performance using new experiments. Whole-game coverage
   requires its own gate; a few representative clips establish feasibility only.

No training was part of the original feasibility archive. The subsequent runtime
integration uses a fresh checkpoint under its explicit observation contract.
