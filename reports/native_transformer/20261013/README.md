# Native Transformer timing, 2026-10-13

This is an isolated native CUDA/cuBLAS backbone benchmark, not a trained policy,
Breakout result, or end-to-end PufferLib SPS measurement. No Python was used for
implementation, correctness checks, deployment, or result processing. The
existing Mario instance and sweep were left untouched.

## Main results

Dedicated NVIDIA GeForce RTX 5060 Ti, 16,311 MiB, compute capability 12.0;
driver 580.105.08; CUDA compiler 12.8.93; 180 W power limit. The GPU had no
allocated memory or GPU utilization before the benchmark. A spot check during
the sweep showed 100% utilization, 65 C, 173.58 W, and 2640 MHz SM clock.

All rows below use BF16 weights/activations, FP32 parameter gradients, 264
tokens per observation, batch 32, and CUDA graphs. Times are per **whole batch**,
not per observation. Each phase has 10 warmups, 20 iterations per repeat, and
five repeats; the table reports the median per-call CUDA-event time.

| Width x blocks | Backbone parameters | Forward, ms | Forward + backward, ms | F+B observations/s | Owned buffers, GiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| 128 x 4 | 793,344 | 2.431 | 5.704 | 5,610 | 0.285 |
| 256 x 4 | 3,159,552 | 5.858 | 13.608 | 2,352 | 0.579 |
| 256 x 6 | 4,739,072 | 8.759 | 20.357 | 1,572 | 0.816 |
| 256 x 12 | 9,477,632 | 17.508 | 40.698 | 786 | 1.526 |

Source: [raw batch-32 BF16 measurements](rtx5060ti/rtx5060ti_bf16_b32.jsonl).
The 256 x 12 configuration matches the Kaggriculture winner's documented
backbone dimensions, not its complete model. The winner's full 10,225,070
parameters also include task-specific components absent here; see its
[architecture document](https://github.com/msdsm/kaggriculture-solution/blob/main/docs/architecture.md).

At fixed width 256, halving depth from 12 to 6 roughly halves time; using four
blocks is roughly three times faster. Width 128 with four blocks is about
7.1 times faster than 256 x 12 in this batch-32 forward+backward measurement.
These differences say nothing by themselves about learning quality.

Additional observations:

- At batch 1, 256 x 12 takes 0.820 ms forward and 2.541 ms forward+backward.
  With graphs disabled, those times are 1.099 ms and 3.200 ms.
- Batch 64 gives 797 F+B observations/s for 256 x 12, only slightly more than
  batch 32's 786. Its owned training buffers use approximately 3.00 GiB.
- FP32 at batch 32 takes 83.453 ms for 256 x 12 F+B, versus BF16's 40.698 ms.
- A **synthetic** 64-token variant of 256 x 12 at batch 32 takes 3.357 ms
  forward and 8.254 ms F+B. It is not a Breakout tokenizer or a Breakout result.
- The memory guard rejected batch 1024 before allocation: approximately
  50.666 GB of model-owned buffers would be needed for the 264-token BF16
  256 x 12 shape. This explicit implementation retains training activations;
  that requirement is not a universal Transformer memory limit.

Full results include BF16 batches 1, 8, 32, 64; FP32 batches 1 and 32;
BF16 eager-dispatch batch 1; and the 64-token diagnostic. Each JSON record
includes min/max repeat timing and a separate host-wall-time measurement.

## Correctness and limits

The three independent FP64 CPU-reference / FP32 CUDA tests passed, including
314 sampled finite-difference derivatives across every parameter tensor and
the input. Maximum observed forward absolute error was 6.01e-7; maximum
sampled gradient absolute error was 3.71e-7. In the separate small BF16 test,
relative L2 errors against identically rounded FP32 inputs/weights were
0.00595 for outputs, 0.00876 for input gradients, and 0.00866 for parameter
gradients. FP32 and BF16 graph-replay comparisons both passed. NVIDIA
Compute Sanitizer memcheck reported zero errors on the test suite. All timed
shapes passed finite-value checks for outputs and all gradients.

Receipts: [numerical checks](rtx5060ti/rtx5060ti_checks.jsonl),
[memory checker](rtx5060ti/memcheck.txt), and
[memory guard](rtx5060ti/rtx5060ti_memory_guard.jsonl).

Attention uses explicit cuBLAS matrices plus separate softmax kernels, not
FlashAttention. No tokenizer, position encoding, action/value head, loss,
optimizer, environment, rollout, or PPO update is included. The throughput
column is **not training SPS**. Memory counts are owned buffers including the
training cache, not total peak allocation or an inference-only footprint.
The component is not yet selectable by the native PufferLib trainer.

The local GTX 1060 FP32 files are smoke-test receipts on a shared WSL/desktop
GPU, not the controlled performance baseline. That run also had a systematic
CUDA-event/host-wall-time discrepancy, so no cross-GPU speed ratio is claimed.

## Reproduction and provenance

Instructions: [native benchmark documentation](../../../tests/NATIVE_TRANSFORMER.md).

```sh
bash tests/build_native_transformer.sh
./build/bench_native_transformer --test
./build/bench_native_transformer --suite --batch 32 --precision bf16 \
    --warmup 10 --iterations 20 --repeats 5 --output new-results.jsonl
```

Base checkout: branch `5.0`, commit
`dea3a1575503db3402695beecd880bd6fa2b67bc`; experimental files were uncommitted.
The remote build received only the three native source/build files, so neither
a full clone nor the PufferLib/Python installer was needed. Source SHA-256:

```text
9738bbe255e488d19951259adb81872a28666c18cb9ea5cd06d511621940e670  src/native_transformer.cuh
e23a1e4760c465fc5c8dbbe1bbe8125d26159ff2f57a8f9ecf68f426272b742e  tests/bench_native_transformer.cu
76a9b0c11addddc9b9438e3de0e90da85ed88d1ca5c6b172d8cab57c858a63ca  tests/build_native_transformer.sh
```

Temporary Vast instance `55811403`, label `native-transformer-temp-20261013`,
was created at 20:59:35 UTC using `vastai/base-image:cuda-12.8.1-auto`, 20 GB
disk, and an advertised total rate of $0.2055556/hour. All 11 remote result/log
files were copied locally and SHA-256 verified before destruction. Destroy
succeeded at approximately 21:06:27 UTC, and the subsequent instance list
confirmed its removal and the original Mario instance `55306751` still running.
The roughly seven-minute rental corresponds to about $0.024 at the listed
hourly rate, excluding any separate transfer charges; this is not an invoice.
The temporary container and its regenerable executable were deleted; source
and measurement receipts remain local.
