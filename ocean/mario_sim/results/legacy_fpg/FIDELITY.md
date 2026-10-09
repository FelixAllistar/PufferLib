# ROM/native fidelity gate — 2026-10-03

Contract v2 passes **13,365 matched trials and 271,490 ROM frames**, on both CPU
and CUDA, in the final overground section of 1-1. The legacy contract fails
1,493 trials under the same capture and action-script scheme. No new training
was run. The former synthetic scores do not establish fidelity, and the active
config starts fresh instead of resuming the old weights.

Low visual fidelity must preserve the state transitions that determine play.
The earlier one-frame controller checks were insufficient: they missed
observation differences and did not test persistent departures from the
reference route. The simulator now has a repeatable gate that tests both.

## What is compared

The harness independently reconstructs the final section's static terrain from
conservative, already-parsed ROM columns across four earlier learned routes.
It excludes the two parser-frontier columns when capturing this fixture, so
the fixture does not depend on the corrected frontier decoder. The terrain and
ROM snapshots stay in ignored local build artifacts; they are not training data.

It captures 1,009 reachable small-Mario states at X=2912..3179, including a
searched FPG suffix. Each native trial copies the initial state once and then
runs independently. **There are no later native state or terrain resyncs.**

Every captured state branches through all twelve legal actions. Every sixteenth
start, plus the FPG reference root, also runs sustained scripts: the reference
inputs, greedy policy, twelve constant controls, reversal, repeated jumps, and
random controls held for 1/4/8/16 frames. Runs last up to 240 frames or first
terminal outcome. This produces 12,108 one-frame branches and 1,257 persistent
trials. Forty-three empty or unsupported reference suffixes are explicitly
reported as skipped, never as passes.

On every frame, the gate compares:

- All 24 player/controller integers, including velocities, subpixels,
  acceleration fraction, jump/button history, collision flags, and flag state.
- All seven camera/parser integers, including scrolling, clamping, parser phase,
  and the flag's actual spawn timing.
- All 71 modeled enemy/timer integers: five normal-enemy slots, movement,
  subpixels, direction, collision bits, stomp/revival timers, and frame phase.
  Inactive slots are canonicalized; scripted scenery uses separate controllers.
- Every visible terrain cell against live ROM RAM, all 448 observation values,
  terminal outcomes, and all twelve policy logits from separate recurrent states.

CPU integer state, observations, and logits must agree exactly. CUDA replays
the same ROM traces without resync: integer state/outcomes must agree exactly;
observation tolerance is 1e-6 for floating arithmetic. It alternates ordinary
kernel launches and CUDA graph replay.

## Results and defects fixed

| Check | Result |
| --- | --- |
| ROM versus native CPU | 271,490 frames; zero state, terrain, observation or logit mismatches |
| ROM versus native CUDA | Same 13,365 trials / 271,490 frames; zero failures |
| Trials exercising the left camera boundary | 208 |
| Frames with a Goomba active | 27,528 |
| Goomba collision outcomes | 11 stomps and four deaths, all matched |
| Pole outcomes across the script panel | 39 FPGs and 245 ordinary grabs |
| Generated reset/suffix checks | 3,072 successful augmented suffixes across v1/v2 |
| Environment CPU/CUDA check | 8,704 decisions, 524 resets, 311 FPGs; complete state, rewards, observations and curriculum agree |
| ASan/UBSan | Pass |
| Legacy negative control | 1,493 failures: 1,308 observation mismatches and 185 controller mismatches |
| Historical selected-policy evaluation | Synthetic and ROM output files reproduce byte for byte with the archived v1 config |

The substantive fixes are:

1. **Parser residency.** At parser phases 0 and 4, the cursor is the next
   unwritten column. Reading it exposed a stale tile from 32 columns earlier.
   The shared decoder now exposes exactly 32 resident columns with the correct
   phase. Both the FPG and older Mario Lab ROM adapters use it.
2. **Shared observations.** Both FPG backends use the same camera window, unknown
   cells `(-1,-1)`, vertical padding, and pole visibility. Hidden pole distance
   is neutral instead of leaking known synthetic geometry or encoding a missing
   pole as a large negative distance. Visible Goombas appear in the second grid
   channel with value 2; value 1 remains the pole. Ego fields 29..31 give the
   nearest visible live Goomba's relative X/Y and horizontal speed.
3. **Camera and flag dynamics.** Scrolling, screen-edge clamping, parser progress,
   and actual flag spawning now evolve in the native state. The flag height is
   not overwritten to conceal a mismatch.
4. **Goombas.** Existing Goombas now have native movement, terrain collisions,
   enemy/player collisions, collision masks, subpixels, stomp timers and death
   outcomes. Their small collision box is constructed at the ROM's point in
   the frame. Screen-edge collision masking and the ROM's wrapped relative-X
   comparison on injury are preserved.
5. **CUDA initialization.** Initial bank/state uploads finish before a
   nonblocking trainer stream can consume them. The ROM-trace test's uploads
   and graph launches also use the same stream.

The legacy control uses the same starts and script scheme, although its greedy
policy's actions depend on its different observations. It reports 13,370 trials
and 38 skips: five reference cases fail observation comparison before reaching
an otherwise empty script. The corrected run reports 13,365 trials and 43 skips.

The frozen v1 checkpoint is an arbitrary recurrent probe in this gate. The pole
outcomes combine scripted and policy actions; they are **not v2 policy scores**.
No causal estimate was made of how much each defect contributed to the old
policy's transfer failure.

## Reproduce

From the repository root, with the locally supplied ROM and saved probe weights:

```sh
make -C ocean/mario_fpg qualify
make -C ocean/mario_fpg sanitize
```

`MODEL`, `CONFIG`, and `PARITY_OUT` can override the probe checkpoint, matching
configuration, and output directory. The default gate uses v2 and the preserved
201.3m-frame checkpoint. `qualify` runs native reset/contract checks, environment
CPU/CUDA checks, independent ROM/CPU rollouts, then ROM/CUDA replay. Any failed
stage stops the command. Failed, interrupted, or filtered CPU runs cannot issue
a qualified trace header.

The old contract is an explicit negative control:

```sh
./build/mario_fpg/parity \
  checkpoints/mario_fpg/1790959626339/0000000134217728.bin \
  ocean/mario_fpg/results/continued_20261002/config.ini \
  build/mario_fpg/legacy_control
```

That command is expected to return failure. [The archived manifest](results/fidelity_20261003/manifest.json)
records source/binary/checkpoint hashes, exact commands, CPU/CUDA summaries,
coverage, failure cases, and the original evaluation replays. Full state traces
and reconstructed terrain remain local under `build/mario_fpg/qualified_v2`.

## Throughput after the fixes

A fresh GTX 1060 benchmark measures **4.39 million game frames/s** for v2,
including observations, resets and reset augmentation, excluding policy inference
and PPO. The same current benchmark with v1 semantics measures 6.13m frames/s;
the fidelity additions cost about 28% of environment throughput on this workload.

An optimized headless QuickNES worker on the i5-7400 measures 74.4k frames/s,
including the same 448-value semantic observation encoding and snapshot resets.
It uses compiled instruction blocks and idle-loop skipping; rendered execution
measures 38.5k frames/s. The GPU result is about 59 times the single headless CPU
worker. This is a batched GPU versus single-worker environment comparison, not
an end-to-end PPO speedup or a matched full-game workload. The measurements and
source hashes are in [throughput_20261003](results/throughput_20261003/manifest.json).

## Scope still to port and qualify

This is a development regression panel, not proof for every action sequence or
all of 1-1. Tested trajectories cover player X=2799..3161 and Y=-24..176, normal
small Mario, existing Goombas, final stairs, and first pole contact. It does not
qualify brick/item interactions, enemy spawning, other enemy types, powerups,
pipe transitions, underground movement, post-flag movement, or full levels.
Graphics, sound and score bookkeeping are outside the modeled training state.

The generated training bank still contains 24 stair/pole courses and no enemies.
The Goomba port adds supported mechanics and regression coverage; it does not
claim that a broader enemy curriculum has been trained. The older Mario Lab
movement/entity model remains unqualified despite its adapter fix.

Before extending training backward through the route, port each missing
mechanic and expand this exact gate to reachable states around it, including
reversals and failed approaches. Add fresh routes as a holdout. Qualify the
expanded scope before spending another training budget on it.
