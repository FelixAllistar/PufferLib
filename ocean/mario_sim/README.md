# Mario simulation, generation and fidelity

The optimized runtime now supplies a CPU/CUDA Puffer environment, seeded reset
generation, and fixed world-data variation. It initializes each episode once and
runs the compiled game logic thereafter. A fresh PPO integration run has completed;
historical simulator-dependent learning conclusions remain withdrawn in the
[experiment audit](EXPERIMENT_AUDIT.md).

The existing hand-written [FPG controller](../mario_fpg/FIDELITY.md) is qualified
only for its final 1-1 panel. Mario Lab is not a qualified full-game simulator.
Neither already supports every mechanic below.

The [feasibility results](RESULTS.md) report the original reconstruction panel and
its boundaries. Those boundaries still apply: this is not exhaustive qualification
of every game route or a generator of arbitrary new level layouts.

## Runtime use

Run from the repository root with the local NTSC ROM, annotated disassembly,
reference action tapes and CUDA toolchain installed:

```sh
make -f ocean/mario_sim/runtime.mk -j2 all bank-builder environment
build/mario_sim/runtime/build_bank build/mario_sim/runtime/generated 16 73
python3 ocean/mario_sim/qualify_runtime.py --output build/mario_sim/runtime/qualified
CUDA_HOME=/usr/local/cuda ./build.sh mario_sim build/mario_sim/puffer --cu
build/mario_sim/puffer train --base.run_id=mario_runtime_fresh
```

The qualification command reuses the archived ROM traces under
`build/mario_sim/qualification_v2`. Supply `--reference PATH` to use another
qualified panel, or create one with `make -C ocean/mario_sim qualify OUT=PATH`.
It checks both optimized backends, generated worlds, successful glitch suffixes,
the complete environment adapter, corrupt banks and corrupt reference traces.
The builder creates the bank only after its reference comparisons succeed.
Bank data and compiled ROM-derived output stay in ignored local build directories.

The default config starts fresh: 2,048 agents, 64 actions, one frame per decision,
2,048 float observations (every RAM byte divided by 256), a 64-wide two-layer
policy, and 268,435,456 training frames. This raw-RAM interface is provisional;
it does not carry over any previous encoder or learning conclusions. It exposes
RAM rather than pixels or all internal timing state. Old checkpoints have a
different observation contract and must not be loaded into this environment.

The reset bank has 493 templates spanning 32 stages, including 119 flagpole
approaches. Sixteen fixed world variants change future enemy types and item
contents while preserving stream lengths, links and instruction bytes. Online
reset knobs vary RNG/clock phase, player subpixels, actor positions/timers and
player form. The default enables clock and subpixel variation. These are
constructed reset states, and the bank does not guarantee every variation is
solvable. The original geometry, resident parser state and other hidden history
come from the templates.

`env.task=1` selects the flagpole-glitch task, with +1 for the verified native
glitch outcome and zero otherwise. Ordinary flagpole grabs, deaths and 1,800-frame
timeouts (about 30 seconds) end the episode. `env.task=0` runs free play and `env.task=2` rewards level
completion. Use `env.fixed_stage=0..31` to restrict templates; an empty selection
is an error. All modes generate and reset on the GPU during CUDA training.

The optional [FPG backward curriculum](../mario_fpg_time/README.md) has its own
environment and `config/mario_fpg_time.ini`. It starts three recorded frames
before a real 1-1 FPG, then moves backward after sustained success while keeping
some easier practice. All starts have matching ROM snapshots and successful
continuations. It retains the configurable timing bonus; the ordinary runtime's
reset distribution and +1/0 reward are unchanged.

For a complete environment benchmark with random actions held eight frames:

```sh
build/mario_sim/runtime/bench_env 4096 1 256
build/mario_sim/runtime/bench_env 4096 0 256
```

These timings include generation, automatic resets, rewards and all observations;
they exclude PPO. Training logs report the separate end-to-end rate. A geometry
viewer is available through `build/mario_sim/puffer eval latest`; it is a RAM
debugging view, not a pixel-accurate NES renderer.

Measured on a GTX 1060 3 GB and i5-7400, with the final 512-byte compiled code
groups, guarded operation fusion, scalar register caching and exact idle/poll
loop skipping:

| Workload | Frames/second | Scope |
| --- | ---: | --- |
| Flagpole environment, 4,096 agents | 113,479 | 1,048,576 frames; resets, generation, rewards and observations included |
| Broad stage environment, 4,096 agents | 69,118 | Same frame count; all 493 reset templates eligible |
| GPU PPO, 2,048 agents | 76,545 | 1,048,576 frames in 13.70 wall seconds, including startup |
| CPU simulation + GPU PPO, four CPU threads | 47,170 | Same training config; 22.23 wall seconds |
| Longer fresh GPU PPO run | 78,871 | 16,777,216 frames in 212.72 monotonic wall seconds, including startup |

The paired CPU/GPU PPO runs produced byte-identical checkpoints. Final replay
qualification matched 1,684,470 reference frames per backend; the adapter test
matched 25,344 decisions, 458 resets and 12 successful episodes across all three
task modes. Six corrupt-bank variants and four corrupt-trace controls were
rejected. Evidence is stored under
[`results/runtime_20261003`](results/runtime_20261003/qualification/summary.json).
The longer run completed with a checkpoint at
`checkpoints/mario_sim/runtime_fpg_20261003/0000000016777216.bin` and the full config
and metrics in `logs/mario_sim/runtime_fpg_20261003.ini`. It is a fresh first run
on this contract, not evidence that the observation design or training settings
are effective for general Mario play.
On reset seed 137, stochastic evaluation returned 14 successes in 2,062 episodes
(0.679%), compared with 16 in 2,177 (0.735%) for the one-million-frame checkpoint.
This run does not establish learning improvement. Both evaluations use the same
template bank and world variants as training, with a different reset seed.

## Inventory

The [machine-readable ledger](coverage.json) separates 50 mechanic families,
their initial-state knobs, and their implementation/qualification status.
The enemy and item checklist was cross-checked against Nintendo's
[original instruction booklet](https://www.nintendo.co.jp/clv/manuals/en/pdf/CLV-P-NAAAE.pdf)
and [Mario history page](https://www.nintendo.com/jp/character/mario/en/history/smb/index.html).
Controller variants, dynamic spawns, geometry and representative locations come
from the locally supplied, fingerprint-verified ROM and its annotated
disassembly. Public character lists alone do not describe all those distinctions.

| Family | Required distinct behavior |
| --- | --- |
| Walking enemies | Goombas; green/red Koopas; Buzzy Beetles; shell kicks, revival, enemy chains and ledge avoidance |
| Flying Koopas | Bouncing, vertical and horizontal Paratroopas; wing loss and resulting walking state |
| Plants | Pipe-created Piranha Plants, proximity suppression, emerge/retract timer and collision |
| Hammer Bros | Walking/jumping/approach logic plus a separate hammer pool, attachment, release and collision timing |
| Lakitu and Spinies | Pursuit, egg throwing, hatching, respawn and shared random state |
| Bullet Bills | Cannon-generated and offscreen-frenzy variants; cannon registry/cooldowns and actor-slot pressure |
| Fish and squid | Both swimming fish, flying fish, and Blooper's pursuit/phase logic; underwater collision rules |
| Castle fire | Podoboo's vertical lava jumps; short/long firebars, speed/direction variants and all segment collisions |
| Bowser | Front/rear slots, motion, fire, late-world hammers, damage, bridge/axe outcome and world-dependent identity |
| Powerups | Mushroom, Fire Flower, Star, 1-Up; emergence, movement, collection, player transformation and timers |
| Player/projectiles | Small/Super/Fire Mario, crouch, swim, vine climb, fireball pool/bounce/hits, injury, star and death phases |
| Blocks/coins | Breakable/bouncing bricks, question blocks, multi-coin timers, hidden coin/1-Up blocks, enemy effects and life counters |
| Hidden routes | Beanstalk/vine creation, growth, climbing and sky-room transition; underground returns and warp zones |
| Platforms | Horizontal/vertical motion, ascending/descending lifts of both sizes, paired scales, dropping and triggered platforms |
| Other terrain | Springs, pipes, bridges, pits, water holes, whirlpools, camera locks and scrolling/parser boundaries |
| Level/game flow | Castle mazes and pipe sequences, checkpoints/restarts, flag/FPG, fireworks/time tally, axe/rescue, next level, hard mode and two-player progress |

Inventorying a behavior does not mark it implemented or tested. In particular,
the absence of an enemy ID from a static level list does not establish absence
from the level: Piranha Plants, cannons, Lakitu's eggs, powerups, and frenzy
controllers instantiate actors at runtime.

`catalog.cpp` inventories 34 distinct area data sets (3 water, 22 ground,
3 underground, 6 castle) and verifies the starting area pointers for all 32
stages by executing the reference ROM's level setup. Stages after 1-1 use an
explicit practice-start selector at the title screen; this is not evidence of
a complete-game playthrough. Shared areas, side rooms and return destinations
remain represented separately from stage labels.

```sh
make -C ocean/mario_sim catalog
```

The generated catalog under `build/mario_sim/` contains metadata, locations,
hard-mode markers and area links. The local ROM and generated logic/data remain
outside the source tree's tracked artifacts.

## Feasibility backend

`generate_logic.py` translates the supplied ROM's immutable control flow into
C/CUDA statements. `logic.h` supplies byte-exact arithmetic, state and input
operations. There is no frame-time opcode decoder. The prototype retains the
original game-state layout, timers, random generator and actor allocation, so
mechanics are not silently approximated while testing feasibility.

This backend is a mechanically translated reference prototype, separate from
the hand-written controller. It still depends on local ROM data and preserves
register/stack semantics. It does not establish that the existing procedural
generator can synthesize every scene. Runtime throughput is measured separately
with the optimized build described above.

Rendering and audio output are omitted. OAM bytes in ordinary RAM are retained:
some collision code reads them. A step follows the physical video-frame boundary,
including instruction cycles, DMA, interrupts, lag, sprite overflow and PPU
status/open-bus timing. It can stop partway through the game's update and resume
on the next call. Unsupported memory reads and control-flow targets fail
explicitly. The sprite-0 timing model assumes the original game's fixed status-bar
marker; changed graphics are outside this prototype's contract.

The 2,048-float debugging observation losslessly exposes all RAM bytes, scaled
by 1/256. The runtime also uses it as a provisional policy input, without making
an encoder recommendation. The eventual policy observation remains undecided; the
old 448-value FPG observation is not an adequate full-game contract.

`reconstruct.cpp` initializes once from a reference start, then compares every
RAM byte, CPU registers, frame-clock phase and debugging observation after each
persistent frame. `cuda_replay.cu` repeats that replay on the GPU, alternating
ordinary launches and CUDA graphs. Neither path copies reference state during
replay. Reference snapshots, inputs and generated traces remain local; compact
results and hashes record the exact qualification panel.

## Reconstruction and generation parameters

`scene.h` provides one reset path for imported clips and constructed scenes.
`parameters.h` and `world.h` expose initial-condition setters usable from C++
and CUDA. Inputs to a replay are an initial scene, fixed world data and a button
tape; future actor positions and spawn events are produced by the simulator.

| Parameter | Current representation |
| --- | --- |
| Player | Position, velocity, subpixels, form, size, star and injury timers |
| Actors | Slot, position, frame/interval timers, powerup type and emergence phase |
| Random state | Seven generator bytes, frame counter and interval phase |
| Difficulty | World number, primary and secondary hard-mode flags |
| Loaded terrain | A validated resident column/row and metatile identity |
| Future level contents | Enemy types and block contents in the world's data streams |
| Hidden history | Initial RAM, registers, sprite data, parser state and video-clock phase |

The last row matters: visible positions alone are insufficient for exact
reconstruction. The prototype retains a complete initial-state template for
fields that have not yet been turned into convenient typed parameters. Its
world setters modify level data before reset and leave instruction bytes intact.
The world-variation panel applies the same fixed data to the reference and native
engines and labels all such cases **constructed**.

The runtime's reset and world-data generator uses this schema. Converting the
existing Mario Lab geometry generator, validating arbitrary new layouts and
choosing a final policy observation remain separate work.
Clip import currently requires emulator state and inputs; video pixels alone do
not contain all the initial state used here.

## Qualification boundaries

The panel covers all 32 stage starts, two scripted exploration patterns, and
independently ROM-validated routes to Bowser, a vine, a powerup and the 1-1 pipe
route. It branches over 16 button combinations and persistent scripts of up to
240 frames. Separate constructed panels vary hidden state, player/actor
parameters and world data. Death and transition frames continue through the same
comparison instead of ending a case when ordinary gameplay stops.

The ledger records actor presence and sampled state changes on matched frames.
It keeps coverage of collisions, phase combinations and complete routes separate.
Rare platforms, complete
castle mazes, all bonus-room routes, every ending sequence and two-player input
still require targeted coverage. No complete-game qualification, pixel-rendering
parity or learning conclusion is implied by these replay results.

Run the complete reconstruction gate from the repository root:

```sh
make -C ocean/mario_sim qualify
```

It requires the existing local NTSC ROM and annotated disassembly. The default
CUDA target is `sm_61`, matching the qualification machine; set `CUDA_ARCH` when
building for another architecture. The GPU verification build disables device
optimization to keep compiler memory bounded. It is unsuitable for throughput
comparisons. The CPU-to-GPU trace format is a local build artifact with explicit
version/size checks, not a portable interchange format.

All four panels must pass CPU and CUDA comparison. Failed, filtered, empty and
truncated traces cannot become successful GPU qualification results. Negative
controls check a corrupted expected state, a reset error, an invalid header and
a truncated trace. Output stays under `build/mario_sim/qualification/` by default.
