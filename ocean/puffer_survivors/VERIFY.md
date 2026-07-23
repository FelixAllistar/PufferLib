# 5c verification

Passed in the migration worktree:

- native CPU build: `bash build.sh puffer_survivors`
- native GPU build: `bash build.sh puffer_survivors --gpu`
- strict C17 CPU smoke/invariant test over 20,000 steps
- finite checks for all 396 observations
- dense-list invariants for enemies, projectiles, drops, and areas
- terminal reward/logging contract test
- native `puf_envs_*` CUDA test compilation
- AddressSanitizer and UndefinedBehaviorSanitizer runs with leak detection disabled

The tool execution environment has no compatible NVIDIA driver, so the compiled
CUDA smoke binary could not be executed here. Run this in the normal GPU shell:

```bash
make -C ocean/puffer_survivors cuda-test
```

LeakSanitizer alone refuses to run under this tool's ptrace wrapper. Run
`make -C ocean/puffer_survivors sanitize` in an ordinary shell to include leak
detection.
