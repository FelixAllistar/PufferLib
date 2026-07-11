# Verification report

The following checks were run in the packaging environment.

## Passed

- GCC 14, C17, `-O2 -Wall -Wextra -Werror` CPU compile.
- 20,000-step CPU smoke/invariant test.
- Dense-list consistency checks for all four entity pools on every smoke step.
- Finite-value check for every one of the 344 observation floats on every step.
- Explicit boss observation assertions.
- AddressSanitizer + UndefinedBehaviorSanitizer 20,000-step run.
- Strict render-header syntax compile against a small local raylib interface stub.
- Strict `binding.c` syntax compile in CPU-only mode.
- Strict `binding.c` syntax compile with `PS_ENABLE_CUDA_VEC` enabled against a
  local vecenv interface stub.
- CUDA implementation host/C++ syntax pass using Clang 17, a CUDA runtime stub,
  and a verifier that preserves kernel bodies while replacing launch syntax.

Run all locally available checks with:

```bash
make verify
```

## Not fully verified here

A real NVIDIA CUDA toolkit, `nvcc`, PufferLib build tree, raylib installation,
and NVIDIA GPU were not available in the packaging environment. Therefore:

- The CUDA files were not compiled by `nvcc` or executed on a GPU.
- The final extension was not linked against the real PufferLib 4.0 `vecenv.h`.
- The renderer was not executed in a real raylib window.
- CPU/CUDA numerical parity was not measured on an NVIDIA device.

The supplied stubs only validate interfaces and syntax; they are not substitutes
for those integration tests. The `make bench-cuda` target is intended to be run
inside your CUDA container.

## Performance note

A direct local comparison used 64 CPU environments for 3,000 measured steps
after 250 warm-up steps, built with GCC 14, `-O3 -march=native`:

```text
modified: 120.0k, 120.9k, 120.9k environment-steps/second
baseline: 115.3k, 116.4k, 112.4k environment-steps/second
```

That is roughly a 4–8% improvement in this particular synthetic run. The final
checksums differed slightly because observation v5 and grid-based radial hit
ordering intentionally change behavior. This is only a directional
microbenchmark; actual PufferLib SPS depends on compiler, CPU, episode occupancy,
and workload mix.
