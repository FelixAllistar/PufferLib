# TrianglePath

A single-agent, fully-observable weighted-path puzzle with an exact `O(H^2)`
DP oracle and a single difficulty knob: triangle height `H`. Designed as an
algorithm benchmark: the optimal solution is trivial to compute offline, the
problem is exponentially large to brute force (`2^(H-1)` paths), and PPO must
learn long-horizon value estimation (greedy local choice fails).

## Rules

- A random triangle of height `H` is generated each episode; cell values are
  iid in `[cell_min, cell_max]`.
- The agent starts at the apex `(0,0)` and takes `H-1` steps, choosing
  LEFT (stay in column) or RIGHT (column + 1) at each row.
- The agent collects the value of every cell it visits (apex through bottom).
- Episode ends at the bottom row. Score = collected sum.

`env.reward_mode=1` (default) emits one terminal reward equal to
`score / (height * cell_max)`. The DP oracle is used only for evaluation.
`reward_mode=0` retains dense per-cell rewards and `reward_mode=2` uses
`score / optimal` as ablations.

## Exact oracle

Backward DP over the triangle:

```text
V(H-1, c) = cells(H-1, c)
V(r, c)   = cells(r, c) + max(V(r+1, c), V(r+1, c+1))
```

`V(0,0)` is the optimal total; the optimal path is recovered by walking down
argmax at each row. Cost is `O(H^2)` regardless of `H`, so the oracle stays
instant while the number of paths grows exponentially.

Verification: `tests/test_dp.c` compares the DP against an independent
brute force over all `2^(H-1)` paths for `H=2..10` and many seeds, and checks
that walking the recovered optimal action reproduces the DP total.

## Benchmark metrics

From `Log` (also exposed as env metrics):

- `score` — agent's collected sum
- `optimal` — DP optimal sum for the same instance
- `regret` — `optimal - score` (primary sweep metric, minimize)
- `perf` — `score / optimal` (optimality)

## Difficulty knob

`env.height` is the only knob. Everything else (observation width, action
space, network shape) is fixed at `TP_MAX_H=64` compile time, so the policy
input is stable while the instance difficulty grows with `H`:

| H | cells | paths | DP oracle |
|---:|---:|---:|---:|
| 8 | 36 | 128 | O(64) |
| 16 | 136 | 32768 | O(256) |
| 32 | 528 | 2^31 | O(1024) |
| 64 | 2080 | 2^63 | O(4096) |

The intended use is to show PPO regret rising smoothly with `H` while the DP
oracle stays trivial, giving a linearly-scalable difficulty dial.

## Files

- `trianglepath.h` — core state, CPU adapter (`puf_*`)
- `trianglepath_solve.h` — exact DP solver (host + device)
- `trianglepath.cu` — GPU-resident SoA adapter (`puf_envs_*`)
- `tests/test_dp.c` — DP vs brute-force oracle verification
- `tests/test_cuda.cu` — CPU/GPU byte-exact parity fuzz
- `../config/trianglepath.ini` — default 20M-step training config

## Build / test

```bash
make -C ocean/trianglepath test          # GPU parity (requires nvcc)
gcc -O2 -I. -Isrc ocean/trianglepath/tests/test_dp.c -lm -o /tmp/tp_dp && /tmp/tp_dp
./build.sh trianglepath --gpu            # native train/eval binary
./puffer train trianglepath
```
