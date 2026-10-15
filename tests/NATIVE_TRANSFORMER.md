# Native Transformer microbenchmark

`src/native_transformer.cuh` and `tests/bench_native_transformer.cu` are an
experimental C++/CUDA/cuBLAS backbone and a standalone numerical/performance
runner. They do not use Python, PyTorch, JAX, or an autograd library. The build
does not modify `./puffer`, any environment, or an existing training run.

The backbone now also has an experimental native PPO adapter for CPU Goofspiel,
selected with `policy.architecture=transformer`; see the
[Goofspiel instructions](../ocean/goofspiel/README.md#native-transformer-comparison).
This standalone benchmark still excludes PPO, tokenization and policy heads.
There is no Breakout or Kaggriculture learning result from this benchmark.

## Build and run

Requires a CUDA development installation, cuBLAS, and a compatible C++ compiler.
The build was checked with CUDA 12.6 on Pascal and CUDA 12.8 on Blackwell.
BF16 requires compute capability 8.0 or newer. Blackwell needs an appropriate
CUDA compiler (12.8 was used here).

```sh
bash tests/build_native_transformer.sh
./build/bench_native_transformer --test
./build/bench_native_transformer --suite --batch 32 --precision bf16
./build/bench_native_transformer --width 256 --layers 12 --heads 8 --ff 1024 \
    --tokens 264 --batch 32 --precision bf16 --graphs 1 \
    --warmup 10 --iterations 20 --repeats 5 --output new-results.jsonl
```

Use `TRANSFORMER_NVCC` to select the compiler and `TRANSFORMER_ARCH` to override
the default `native` GPU target. An optional first build-script argument selects
the executable path. `--output` creates a new file exclusively; it refuses to
overwrite an existing result. Both tests and benchmarks emit JSON Lines.

`--suite` selects width/layers `128x4`, `256x4`, `256x6`, and `256x12`, with
four heads at width 128, eight at width 256, and FFN width four times model width.
Batch and token count stay fixed. Individual configurations accept width, depth,
head count, and FFN width independently; width must be divisible by head count.

## What is measured

Each block is affine pre-LayerNorm, dense noncausal multi-head self-attention,
residual addition, affine pre-LayerNorm, two-layer 4x FFN with tanh-approximate
GELU, and residual addition. A final affine LayerNorm follows the stack. FFN
width is configurable. Attention materializes the score/probability matrix and
uses cuBLAS GEMMs plus separate softmax kernels. **It is not FlashAttention.**

Weights and intermediate tensors are FP32 or BF16. Matrix accumulation,
normalization/softmax reductions, and parameter-gradient buffers are FP32.
The backward pass computes input gradients and gradients for every trainable
matrix, bias, and normalization parameter. Each call overwrites gradients;
cross-minibatch gradient accumulation is not implemented.

Inputs, upstream gradients, and weights are fixed pseudorandom values (seed 17).
Timing excludes initialization, allocation, host/device transfers, tokenization,
position encodings, task-specific action/value heads, loss calculation,
optimizer updates, environment stepping, and PPO orchestration. There are no
attention masks, temporal memory, padding, dropout, or checkpoint loading.
The 264 tokens are tokens **within one synthetic observation**, not 264
environment time steps.

Forward, backward using saved forward activations, and forward+backward are
timed separately. CUDA graphs default to on; `--graphs 0` includes ordinary
host dispatch. Each phase has warmups and multiple timed repeats. Reported
median/min/max are per-call CUDA-event times; `wall_ms` is independently measured
host elapsed time per call. `observations_per_second` is batch divided by this
isolated call time, **not training/environment SPS**. Graph creation, finite
checks, and result serialization are outside the timed regions.

`owned_cuda_bytes` counts the model's CUDA buffers, including saved training
activations and backward scratch even for forward-only timing. It is not an
inference-minimized footprint, total device memory use, or measured peak memory;
CUDA/cuBLAS context and graph allocations are excluded. The runner skips a shape
when its estimated buffers plus 256 MiB headroom exceed currently free memory.

## Correctness checks

`--test` checks three small FP32 configurations against an independent FP64
CPU forward implementation. Central finite differences test selected entries
from every parameter tensor and the input, including nonstandard dimensions
and one-token attention. Repeated backward calls are checked for unintended
gradient accumulation. On supported GPUs, BF16 output, input gradients, and
parameter gradients are compared against FP32 using identical rounded weights
and inputs. FP32/BF16 graph replays are checked against eager execution after
poisoning output/gradient buffers. Timed configurations are checked for finite
outputs and all gradients before timing and directly after the final replay.

For a CUDA memory check:

```sh
compute-sanitizer --tool memcheck --error-exitcode 99 \
    ./build/bench_native_transformer --test
```

These checks are useful evidence, not exhaustive proof or a learning-stability
test. The implementation remains experimental.

## Size anchor and interpretation

The Kaggriculture winner's documented final architecture is width 256, 12
blocks, 8 heads, FFN width 1024, and 264 tokens. Its full model has 10,225,070
parameters; see the [winner's architecture document](https://github.com/msdsm/kaggriculture-solution/blob/main/docs/architecture.md).
This benchmark matches those backbone dimensions, **not the full winner**:
tokenizers, selective positional encoding, output heads, and other task-specific
pieces are absent. Its backbone has 9,477,632 parameters.

For model width `D`, FFN width `F`, and depth `L`, this implementation's exact
parameter count is `L * (4*D*D + 2*D*F + 9*D + F) + 2*D`. The suite counts are
793,344; 3,159,552; 4,739,072; and 9,477,632. Token count affects compute and
activation storage but not these backbone parameter counts.

The measured speeds describe this explicit-attention implementation and GPU,
not an optimized Transformer speed limit. Comparisons to another architecture
must account for observation tokenization, model size, precision, batch size,
temporal state, and what the timed region includes. A separate native policy
integration and a controlled learning experiment are needed to compare agents.

Initial measurements and validation receipts are in
`reports/native_transformer/20261013/`.
