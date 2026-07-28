# Bomberman environment-only rework

This package changes **only the Bomberman environment**. It does not replace or
patch `pufferl.cu`, `puffercpu.h`, `pufferenv.h`, or any other shared PufferLib
source file.

Copy these files over the existing Bomberman environment directory and keep your
normal PufferLib checkout unchanged.

## Important compatibility note

Start a fresh model. The observation size and semantics changed, so old
checkpoints are not compatible.

## Observation redesign

`OBS_SIZE` is now **1200 standard float values in [0, 1]** per agent. This uses
PufferLib's existing float observation path without framework modifications.
The old observation contained 1611 floats.

Every agent sees a canonical board orientation: its spawn corner is mirrored to
the top-left. This removes the need for a shared policy to learn multiple rotated
or mirrored copies of the same tactic.

Each cell has eight channels:

1. hard wall
2. soft block
3. visible item kind
4. own bomb fuse urgency
5. enemy bomb fuse urgency
6. predicted earliest blast danger
7. self position
8. opponent position

The global block adds game progress, current danger, nearest-opponent proximity,
bomb capacity, whether a newly planted bomb would hit an opponent or soft block,
whether an escape route exists, escape margin, legal-action bits, living-opponent
fraction, and legal-move fraction.

The per-agent blocks contain alive state, canonical position, bomb capacity,
range, speed, active bombs, movement cooldown, and spawn invulnerability.

PufferLib's normal CPU-environment action-mask binding is used when available.
The same legal actions are also included in the observation. The optional CUDA
environment keeps the standard PufferLib GPU interface and therefore treats all
actions as sampleable; invalid actions remain safe no-ops in the simulator.

## Simulator fixes

- Bombs lose exactly one fuse tick per environment step.
- Chain reactions resolve in the same step without fast-forwarding unrelated bombs.
- Flame duration and bomb fuse timing are no longer off by one.
- Simultaneous deaths and overlapping flames assign kill credit consistently.
- Movement conflicts are resolved symmetrically instead of by agent index.
- Mirrored agents use mirrored directional controls correctly.
- Movement cooldown timing is consistent.
- Bomb lookup uses a direct cell-to-slot index rather than scanning every bomb.
- Predicted danger includes chain reactions.
- Timeout draws can apply `reward_timeout` to discourage camping.

## State and hot-loop changes

- `BMMatch` is approximately 1352 bytes with this compiler, down from roughly
  2900 bytes in the supplied version.
- `BMBomb` is 8 bytes instead of roughly 24 bytes.
- Board bounds match the configured 13x11 arena, avoiding unused 15x15 state.
- Observation count falls from 1611 to 1200 floats, reducing model input and
  rollout storage by about 25.5%.

## Included files

Modified:

- `bm_constants.h`
- `bm_sim.h`
- `bomberman.h`
- `bomberman.c`
- `bomberman.cu`
- `bomberman.ini`
- `test_sim.c`
- `Makefile`
- `README.md`

New:

- `bench_sim.c`
- `VALIDATION.md`
- `CHANGES_MANIFEST.txt`

No shared PufferLib core files are included.

## Tests

```bash
make test
make sanitize
make bench
```

The tests cover exact fuse timing, chain reactions, danger prediction,
simultaneous movement, mirrored controls, movement cooldowns, simultaneous kill
credit, observation channels, CPU action masks, timeout penalties, randomized
rollouts, and deterministic transitions.

## Suggested configuration

The included `bomberman.ini` is conservative for a 4-core CPU and 3GB GTX 1060:

```ini
total_agents = 1024
num_buffers = 2
num_threads = 4
gpu_env = 0
minibatch_size = 4096
horizon = 64
```

`gpu_env = 0` keeps the environment simulation on the CPU while PPO/model work
uses the GPU, which is normally the safer setup for this hardware and preserves
standard CPU-side action masks.
