# Validation

## Completed here

- GCC optimized build with warnings treated as errors
- Standalone simulator correctness suite
- AddressSanitizer
- UndefinedBehaviorSanitizer
- Leak detection
- Randomized deterministic rollout test
- CPU step-plus-observation benchmark
- Source check that no shared PufferLib files or custom core macros are present
- Source check that CUDA reset/step signatures match the supplied PufferLib core

## Latest local benchmark

On the available sandbox CPU, 512 matches for 2000 iterations produced roughly:

- 574,000 match-steps/second
- 1,149,000 agent-steps/second

This is a microbenchmark of simulator step plus both agents' observations, not a
PPO training benchmark.

## Not available here

- CUDA compilation (`nvcc` is unavailable)
- NVIDIA runtime testing
- Full PufferLib PPO training

The CUDA source uses the original PufferLib GPU environment function signatures
and no longer requires any framework patch.
