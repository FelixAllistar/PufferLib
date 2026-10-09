# Mario learning experiments

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

The main objective is fast, reliable, glitchless and warpless full-game play.
The first experiment trains an original CUDA platformer on generated courses,
then tests whether its policy can clear real SMB1 World 1-1. Simulation speed,
learning speed and transfer are separate measurements. None follows automatically
from domain randomization.

The original pixel and ROM environment remains a baseline. Routine synthetic
training is paused pending the expanded fidelity gate. ROM execution is useful for calibration, transfer
evaluation, replay/data capture, an explicit Go-Explore experiment, and optional
pixel-plus-ROM novelty runs. The main synthetic engine uses original code and
handcrafted/generated geometry; it does not import the retired native Mario port,
ROM level data, or graphics. Reference measurements can calibrate its controller.

## Experiment register

Identifiers stay stable in run manifests. A working simulator is an implementation
milestone; it does not establish that a policy learned or transferred.

| ID | Experiment | Main comparison | Status and dependency |
| --- | --- | --- | --- |
| CUDA001 | Generated 1-1-like courses with egocentric semantic observations | Train on synthetic layouts; evaluate unseen layouts and real 1-1 | Implemented; real 1-1 clears observed without ROM training; [transfer results](../mario_sim/results/legacy_lab/TRANSFER_RESULTS.md) |
| OBS001 | Structured encoder ablation | Flat MLP versus tile CNN plus entity encoder, independently trained | MLP baseline first; CNN planned |
| GEN001 | Geometry and curriculum ablations | Fixed templates, shuffled templates, randomized dimensions, adaptive sampling | Seeded templates, authored practice starts, independent short/full goals and bounded adaptive rounds implemented; [results](../mario_sim/results/legacy_lab/CURRICULUM_RESULTS.md) |
| GEN002 | Continuous full-level randomization | Root starts on changing 256–512-tile generated worlds versus the short-course curriculum | Optional ablation, deprioritized: long empty stretches did not address the intended continuing curriculum; two smoke runs only; [continuation plan](../mario_sim/results/legacy_lab/CONTINUING_TRAINING.md) |
| PHY001 | Controller calibration and physics randomization | Calibrated fixed physics versus bounded parameter variation | Eight reference trajectories, optional measured profile and ROM adapter implemented; collision/entity calibration and dynamics randomization remain open |
| VID001 | Visual pretraining from YouTube TAS footage | Temporal/self-supervised features or inferred-action imitation versus scratch | Planned; action inference and video alignment needed |
| TAS001 | Behavior cloning from emulator movie inputs | BC initialization plus online learning versus online learning alone | Planned; verified action/state alignment required |
| TAS002 | Resets from TAS trajectories | Root starts versus mixed root/demo starts; optionally expand backward | Synthetic reset comparisons implemented as a foundation; verified TAS starts still require data/snapshot adapters |
| TAS003 | Generate our own tool-assisted demonstrations | Bounded trajectory search versus scripted and learned teachers | Planned; reuse deterministic core snapshots |
| EXP001 | Go-Explore on the CUDA simulator | State archive plus return-and-explore versus ordinary exploration | Planned |
| EXP002 | Go-Explore on the real ROM | Exact emulator restoration and action-sequence branching | Explicitly included; independent of ordinary ROM PPO |
| QD001 | MAP-Elites and CMA-MAE behavior search | Diverse behavior archive versus scalar reward/config search | Adapt Kaggriculture orchestration after CUDA001 |
| INT001 | Concept probes and causal interventions | Paired scenarios, hidden-state probes, ablations and activation patching | Planned; can start with the surviving pixel policy |
| SKILL001 | Precision and glitch practice, including FPG | Terminal-only reward, adaptive reset distance and subpixel variation; fidelity gate before training | V1 trained through 335.5m but failed stronger fidelity tests. V2 passes 271,490 matched ROM/CPU/CUDA frames in the final 1-1 section; no v2 training yet. [Fidelity](../mario_sim/results/legacy_fpg/FIDELITY.md), [historical results](../mario_sim/results/legacy_fpg/RESULTS.md) |
| PIX001 | Raw pixel plus ROM learning | Preserved full visual/emulator baseline | Optional novelty/control experiment |
| DIST001 | Distill the surviving policy into another encoder | Successful teacher trajectories versus fresh initialization | Optional; preserve teacher provenance and recurrent history |
| GAME001 | Extend to water, moving platforms, castle/boss and maze sections | Per-family mastery and then full-game composition | Active feasibility and coverage work, before any further training |

## First implementation and gates

CUDA001 starts with small-player horizontal movement, acceleration, braking,
variable-height jumps, subpixel state, tile collisions, walking enemies, pits,
stairs, pipes and a separate underground room. A CPU reference executes the same
integer controller as the CUDA backend. The initial controller is approximate;
CPU/CUDA agreement proves implementation consistency, not Mario accuracy.

The initial observation is a local semantic grid relative to the player, player
kinematics and relative entity records. Background colors do not enter the
observation. Pits are derived from missing support geometry. Tile-local position
and fractional pixels remain visible because they affect takeoff and collision
timing. The first encoder is an MLP with recurrent memory; later experiments can
replace it without changing other policy architectures.

1. Port the relevant real level geometry into a local evaluation fixture. From
   identical reachable starts, compare persistent ROM/native trajectories under
   all buttons and varied action sequences: integer state, observations,
   entities, camera, outcomes, and policy outputs. Never resync the native state
   between frames. Require the same gate on CUDA, plus reset/replay checks,
   before training that scope. The [FPG gate](../mario_sim/results/legacy_fpg/FIDELITY.md) implements
   this for the final 1-1 section. CPU/CUDA self-agreement alone is insufficient.
2. Measure environment throughput with observations and resets included. Measure
   PPO throughput independently. Both report decisions and simulated frames.
3. Train a small baseline, then measure per-family clear rate on fixed unseen
   seeds. Preserve failures as replayable examples. Reachability of arbitrary
   generated combinations needs verification, not just plausible dimensions.
4. For each expansion, port and qualify its acceleration, braking, jump phases,
   hitboxes, button sampling, entities and transitions before adding them to
   training. Keep the observation/action adapter identical across the engines.
5. Evaluate a frozen synthetic-trained policy in real 1-1 without ROM training.
   Record clear rate and complete action tapes, plus controller/observation
   discrepancies. A synthetic imitation of 1-1 is not this transfer test.
6. Expand generation and game families in response to measured failures.

Initial seeds have separate training, validation and held-out test streams.
Different streams can still produce similar templates. Structural holdouts
(new obstacle combinations, widths, entry velocities and pipe arrangements)
are an additional gate. Do not tune repeatedly on the final seed bank.

## Original geometry and controller

Use handcrafted motif families with constrained variation: run-up, obstacle,
landing and recovery space. Randomize gap width, obstacle height, spacing,
enemy placement and route composition within tested ranges. Add a reachability
oracle or search-based rejection before advertising every generated course as
solvable. Preserve seed and generator version for every failure.

Pixel and subpixel precision matter for ordinary fast movement as well as
glitches. Preserve fractional position, velocity, collision extents and button
edges. Exact reproduction of every console exploit is not a main-engine goal.
FPG practice is a separate task with its own mechanics/verification requirements.

Start with fixed controller parameters. Geometry randomization and physics
randomization are independent switches and experiments. Broad, implausible
physics variation can train the wrong controller or conceal a systematic error.
When varying dynamics, specify whether the policy observes the parameters.

## Demonstrations and resets

Prefer emulator movie inputs when they exist. TASVideos publications can provide
both videos and machine-readable input movies. For example, [this SMB1 warpless
publication](https://tasvideos.org/3728M) offers an FM2 movie, but is explicitly
tagged for heavy glitch use. Warpless does not imply glitchless. Tag each source
and segment with its ruleset before using it for the main task.

For each movie: record source, author, file hash, ROM identity, emulator/version,
region, starting state and controller/frame convention. Replay once using a
compatible reference and verify landmarks. Export aligned pre-action observations,
actions, native frame counts and complete snapshots. Do not assume an FCEUX movie
replays identically in QuickNES without checking input/boot alignment.

TAS001 uses those action labels for behavior cloning. Keep train and evaluation
trajectories separate. Demonstrations are narrow, so also collect recovery states
around them and evaluate closed-loop failures. A new encoder can learn from the
same actions without sharing the teacher's representation.

TAS002 changes the distribution of starting states. Sample useful intermediate
states while retaining ordinary game starts; move starts backward or adjust
sampling by measured success. Preserve clocks, enemy phases, controller history,
and reward bookkeeping. Reconstruct recurrent memory using a prefix/burn-in for
the current policy. A raw NES snapshot is not a CUDA-engine snapshot: an explicit
state mapping or replay in a calibrated engine is required.

[Data-Augmented Game Starts](https://arxiv.org/abs/2605.14379) studies intermediate
offline-data starts in two-player imperfect-information games. Its results do
not directly establish Mario transfer. Here the proposed adaptation is a
single-player reset curriculum; [Backplay](https://arxiv.org/abs/1807.06919)
provides a related demonstration-based backward curriculum.

VID001 remains distinct. Ordinary YouTube footage has no authoritative action
labels or recoverable emulator state. Start with visual/temporal pretraining, or
train an inverse-dynamics model on aligned simulator frames and known inputs.
Validate inferred buttons against held-out paired recordings. Editing, dropped
frames, overlays and compression need alignment checks. Existing input movies
can generate labeled frames without estimating buttons from video.

TAS003 can use beam search, tree search or trajectory optimization over cloned
synthetic states, then replay selected candidates in the relevant backend.
Save unsuccessful branches as recovery data. A bounded search result is an
expert candidate, not a proven perfect TAS.

## Exploration quality diversity and interpretability

[Go-Explore](https://arxiv.org/abs/2004.12919) motivates separate state archives
for CUDA and ROM backends. Store full backend snapshots and controller prefixes;
index coarsely by area, position and movement/power state, retaining timing and
velocity diversity within cells. A reachable archived state and a reliable
policy for reaching it are separate results. Archives must survive interruption.

QD001 adapts [Kaggriculture's coordinator](../kaggriculture/qd.py). Keep its
distinction between training reward and evaluated quality. Suggested behavior
descriptors include pipe-route use, airborne fraction and powerup strategy.
Evaluate complete runs, completion reliability and elapsed native frames; keep
personal-best speed as an additional statistic. Sparse regions and novelty are
exploration signals, not evidence of faster reliable completion.

INT001 starts with paired valid states and action-probability changes. Record
encoder activations and recurrent state; probe concepts such as landing safety,
enemy approach, water movement and pipe opportunity. Test causal hypotheses by
ablating or replacing activations. [Maze policy analysis](https://arxiv.org/abs/2310.08043)
is a relevant example. Probe accuracy alone does not show the policy uses a
concept. Keep evaluation probes separate from auxiliary concept supervision.

## Run records and comparison

The initial Mario Lab training track has been retired; use
[mario_sim](../mario_sim/README.md) for current training. Historically, each
run gets a unique directory with a manifest, copied source/config inputs, binary
identity, full command, console log, native checkpoints and the resolved run INI.
Record the experiment ID and parent checkpoint. Preserve failed runs and label
smoke tests. The launcher uses private configs and an explicitly named binary.

Compare fixed wall-clock budgets as well as fixed frame budgets, with multiple
seeds after the first smoke. Report learning curves and per-family results.
Pure simulator throughput excludes policy inference and training. Include reset
costs, observation production, hardware, batch size and workload in benchmarks.

The eventual full-game target needs a fixed glitchless/warpless ruleset,
start/finish convention, failure/retry accounting and complete replay evidence.
Internal simulator times are not a world record. Keep the all-level panel,
individual trick tests and full-game evaluation separately labeled.

Native PufferLib checkpoints currently resume weights; exact
optimizer/scheduler/RNG resume requires additional support.
