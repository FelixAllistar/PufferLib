# Full-level training setup — 2026-10-01

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

Normal training now reads [config/mario_lab.ini](../../config/mario_lab.ini)
directly through `./build/mario_lab/puffer train`. The config requests one
continuous 500-million-frame run from fresh weights, without a staged curriculum
or practice resets. That full budget has not been trained yet.

The earlier [1-1 transfer results](TRANSFER_RESULTS.md) came from the short-course
curriculum and its 64-wide policy. They are not scores for this new setup.

## Configuration and generation

`generator_mode=1` creates complete courses with one root start, one finish,
one episode clock and no intermediate section rewards/goals. Surface length
varies between 256 and 512 tiles. Random 40–96-tile sections vary style,
difficulty, motif dimensions and spacing; mixed sections combine gaps, pipes,
stairs, walkers, overhead blocks and flat ground. Optional underground routes
have varied entrance/return locations and heights, plus 48–128-tile rooms with
varied overhead blocks. A requested underground-family episode requires the
ordinary pipe return before completing.

The enemy pool now holds 64 authored walkers over the full world. The nearest
eight visible entities occupy the existing observation slots. New terrain
storage covers positions past the previous 192-tile cap. The input remains
1,128 egocentric semantic floats with no absolute position or level/section ID.

The policy is an MLP with two MinGRU layers, hidden size 128. It uses 256 parallel
CUDA environments, 256-frame rollouts and recurrent state carried between
rollouts until termination. The episode cap is 8,000 frames; discount is 0.9995,
GAE lambda 0.99, learning rate 0.001 and entropy coefficient 0.01. Frontier shaping
is 0.001 per new forward pixel; completion pays 10 plus up to 2 for speed, and
death/timeout costs 1. The measured small-player controller is mode 2.

These are original generated levels, not extracted ROM levels or the full Mario
game. The generator still uses a small set of primitives and an approximate
walker model. Global solvability, learning reliability and improved real-game
transfer remain unestablished. Longer courses do not fix contact/bounce mismatch.

## Checks and measured speed

- CPU fixtures and ASan/UBSan passed, including the ROM observation fixtures.
- 400 full-world generation/replay checks produced 204 distinct surface lengths,
  all 81 supported bonus-room widths and up to 19 authored walkers in the mixed
  sample. Checks cover terrain collision across the old storage boundary, local
  observations of later enemies and completion of a 512-tile flat course in over
  3,000 frames. They do not prove every random course is solvable.
- All 19 CPU/CUDA parity cases passed, including full-world terrain/entity state,
  practice comparisons, terminal resets and CUDA graph execution. The check
  caught and fixed unspecified ordering of two RNG draws in a function call.
- A native 1,048,576-frame launch and a separate 4,194,304-frame launch completed
  with the new config, 256-frame rollouts and finite saved policy weights. The
  latter reached 8,000-frame episode caps and ordinary terminal resets.
- Native checkpoint reload/evaluation completed. It cleared zero courses in 256
  vector-completed attempts; only 32 were requested, demonstrating why the native
  vector check is not an exact episode-count panel. These short runs validate
  execution, not mastery.
- The full generator executes **1.93 million frames/s** on the GTX 1060 3GB:
  16,777,216 frames in 8.711 seconds, including scripted actions, observations and
  resets, excluding PPO/inference and 64 warmup steps. The retained measurement
  ran after training/evaluation completed. The old 2.95-million measurement was
  for the smaller generator.

See [qualification.json](results/full_levels_20261001/qualification.json),
[benchmark.json](results/full_levels_20261001/benchmark.json) and the
[native training log](../../logs/mario_lab/native_full_levels_check/mario_lab/full_levels_terminal_check_20261001.ini).

## Retained comparisons

`generator_mode=0` keeps the earlier short generator. Observations, rewards and
the original 6,140-byte state prefix match the archived v4 engine bit-for-bit
over 32,768 compared decisions across controller modes 0, 1 and 2; see
[legacy_parity.json](results/full_levels_20261001/legacy_parity.json).
The v5 state is 17,380 bytes. Raw snapshots require their matching version/binary.

The old runs and their binaries remain available. Portable
[short-transfer](presets/short_transfer.ini), [legacy-baseline](presets/legacy_baseline.ini)
and [full-level](presets/full_levels.ini) INIs permit explicit config comparisons.
Optional Python archival helpers retain the old short-course workflow; they are
not required for normal native training.
