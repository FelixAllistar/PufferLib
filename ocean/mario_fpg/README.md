# Mario FPG precision experiment — SKILL001

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

This experiment tests the flagpole glitch (FPG), separate from the main
glitchless/warpless objective. Ordinary low flag grabs do not count.
The success condition requires grabbing at raw player Y >= 162, entering the
end-of-level routine immediately, and leaving the flag at its original height.

**Current work is simulator qualification, not more training.** Contract v1
trained through 335.5m frames, but failed the stronger state/observation parity
gate. Its selected 201.3m checkpoint and [recorded results](RESULTS.md) remain
archived diagnostics. Contract v2 fixes the identified differences and starts
with `load_model_path=None`; no v2 training result is claimed. See the
[fidelity report](FIDELITY.md) for the matched ROM/native comparison and scope.

The controller is original integer C/CUDA code. Horizontal speed uses 1/16 pixel
units, position retains 1/256-pixel accumulators, and acceleration has a separate
fractional accumulator. Collision probes, button edges, jump phases, braking,
and collision ejection order are modeled explicitly. The older Mario Lab AABB
controller has subpixels but cannot express this interaction.

The qualified scope is small Mario in the final overground section of 1-1,
including stairs, the pole, camera boundaries, and existing Goombas. Brick
interactions, other enemy types and spawning, swimming, powerups, pipe animation,
and the castle walk still need ports and ROM tests. A pipe-shaped obstacle in
the viewer is geometry only.
The longest reset currently starts partway up a staircase. **This is not yet a
pipe-exit-to-pole policy or a complete Mario level.**

## Use

From the repository root (`/home/felix/puffertank/pufferlib`):

```sh
source /home/felix/puffertank/venv/bin/activate
make -C ocean/mario_fpg qualify
CUDA_HOME=/usr/local/cuda bash build.sh mario_fpg build/mario_fpg/puffer --cu --float
```

Edit [`config/mario_fpg.ini`](../../config/mario_fpg.ini) directly. It currently
uses a fresh H64/L2 MinGRU with a proposed budget of 268,435,456 frames, 256
environments, one frame per decision, and terminal reward +1 for FPG / 0 otherwise.
There is no progress, coin,
speed, or ordinary-completion shaping. Native checkpoints/config logs go to
`checkpoints/mario_fpg` and `logs/mario_fpg`. Continuing from a checkpoint requires
setting `base.load_model_path`; architecture and observation contract must match.
The native checkpoint is weights-only: restarting does not restore optimizer,
recurrent states, or the running curriculum estimates.

The v2 observation meanings changed even though the shape remains 448 floats.
Do not resume a v1 checkpoint under the v2 config. To reproduce the historical
policy, use its archived v1 config in `results/continued_20261002/config.ini`.
The rebuilt evaluation tools retain v1 behavior. A future compatible continuation
uses `train.total_timesteps` as the **additional** budget; its native run counter
and sampler estimates restart.

The checkpoint filename counts frames within its native run: the selected model
has 67,108,864 parent frames plus 134,217,728 continuation frames. The completed
continuation's final checkpoint, `0000000268435456.bin` in the same directory,
has 335,544,320 total frames. Selection uses synthetic validation scores, not ROM
results. The exact training config and lineage are archived in
[`continued_20261002`](results/continued_20261002/manifest.json).

```sh
make -C ocean/mario_fpg viewer
./build/mario_fpg/viewer
```

Arrows move, X/Space jumps, Z/Shift runs. R restores the current start; N draws a
new one; 1–4 select reset distance. The viewer defaults to the stair start;
the display shows native pixel/subpixel state.

## Curriculum and reset data

The CPU bank builder searches original generated stair/pole courses. It stores
only verified successful synthetic trajectories. It neither links an emulator
nor reads a ROM, movie, or the ROM reference inputs. Bounded search failure means
“not solved by this search budget,” not a proof of impossibility.

| Tier | Reset distance along a successful synthetic trajectory |
| --- | --- |
| 0: contact | 1–8 frames before success |
| 1: approach | 9–32 frames before success |
| 2: jump | 33–96 frames before success |
| 3: stairs | The trajectory's original stair start |

Every tier retains 10% of sampling. The remaining 60% targets the earliest weak
tier, reconsidered after each episode. A tier needs 32 observations and a smoothed
success rate of 75% before focus advances. Forgetting brings practice back. These
are per-environment estimates; this is a small continual sampler, not yet a
whole-game failure diagnosis system. The policy and optimizer remain in the same
training run throughout these changes.

Geometry and initial controller state vary by seed. At each reset, horizontal
subpixel and acceleration phases are randomized; a candidate is accepted only
if replaying the known synthetic suffix still succeeds. No demonstration actions
enter policy observations or PPO losses. This is **reset assistance, not behavior
cloning**. It deliberately gives a terminal-only learner achievable early trials.

The current bank has eight courses per split (24 total); 32 candidate courses
were searched to obtain them. Stair heights range from 6–8 and gaps from 6–10
tiles. Every accepted solution was replayed against the final core. The bank is
finite, and search introduces selection bias. Separate seed splits
do not establish structural generalization. Augmentation adds phase variation;
it does not make the small set of geometry families infinitely diverse.

To build a different bank without overwriting the recorded one:

```sh
make -C ocean/mario_fpg bank-builder
./build/mario_fpg/build_bank build/mario_fpg/another_bank.bin 16
```

The last argument is cases **per split** (training, validation, test). Point
`env.reset_bank` at that file. Files are versioned native little-endian records;
the loader checks every stored controller state and replays every solution before
allowing training. Keep the matching bank with the run; logs print its fingerprint.
Bank generation is a one-time CPU search and is much slower than rollouts. The
checked bank is already included; ordinary training does not regenerate it.

## Accuracy and evaluation

The ROM is an evaluation oracle. The supplied reference prefix is an earlier
learned policy's natural 1-1 pipe route. A new bounded simulation search supplied
a 100-frame FPG suffix from frame 1510 on the stairs. Replaying it in QuickNES
matches the controller state every frame and gives Y=164, flag Y=48, routine=5.
There are no RAM writes. This is a local tool-assisted example, not a perfect TAS,
and no BC has been performed. Provenance is in [`reference/provenance.json`](reference/provenance.json).

```sh
make -C ocean/mario_fpg reference
./build/mario_fpg/reference ocean/mario_fpg/reference/learned_pipe_route.actions --replay 1510 ocean/mario_fpg/reference/fpg_suffix.actions
./build/mario_fpg/reference ocean/mario_fpg/reference/learned_pipe_route.actions --end-only
make -C ocean/mario_fpg test gpu-test sanitize
make -C ocean/mario_fpg bench
```

The older `--end-only` diagnostic checks all 12 legal actions from each eligible real state after
X=2800, through the initial pole grab. Without it the earlier region is also
checked and deliberately exposes missing interactions. It does not compare
observations or persistent native trajectories. The mandatory stronger check is
`make -C ocean/mario_fpg qualify`: independent persistent CPU and CUDA rollouts,
integer state, observations, terrain, and recurrent policy logits. It stops at
the first failing stage. The ROM fixture is a local evaluation artifact;
training still uses original generated courses.

Exact-count policy evaluation (replace `MODEL` with a checkpoint path):

```sh
make -C ocean/mario_fpg policy-eval transfer
./build/mario_fpg/eval MODEL MATCHING_CONFIG 64
./build/mario_fpg/transfer MODEL MATCHING_CONFIG ocean/mario_fpg/reference/learned_pipe_route.actions 1510 ocean/mario_fpg/reference/fpg_suffix.actions 32
```

Both report each reset tier separately, with greedy and sampled actions. ROM
evaluation restores reachable states on the reference suffix and resets policy
memory. It records every action, ordinary grabs, timeouts, and one-frame
controller discrepancies, and replays each action tape to verify the complete
NES state. Repeated evaluation on this reference is development
feedback, not an untouched final test. It does not establish reliability from
other pipe-exit phases, other routes, or arbitrary game starts.

`transfer` also accepts `random`, `right` (constant right+run), or `idle` in place
of `MODEL` for controls; `eval` accepts `random`. In greedy ROM evaluation the
32-trial contact panel repeats eight distinct starts four times. The stair panel
repeats one start. Count unique setups as well as rollouts. Results and the exact
pilot configuration are under [`results/pilot_20261002`](results/pilot_20261002).

Next gates: extend the same fidelity tests backward through brick and pipe
interactions and enemy spawning; hold out fresh action routes; then train and
evaluate the corrected contract. Keep FPG metrics separate
from glitchless clear speed. The broader plan is
[`CONTINUING_TRAINING.md`](../mario_lab/CONTINUING_TRAINING.md).

Mechanics references: [annotated SMB disassembly](https://6502disassembly.com/nes-smb/SuperMarioBros.html)
and [TASVideos game resources](https://tasvideos.org/GameResources/NES/SuperMarioBros).
The implementation uses mechanics as a specification; it does not embed ROM code,
level dumps, or graphics in the trainer.
