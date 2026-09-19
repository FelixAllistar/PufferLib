# Compiled ROM CPU

This backend accelerates the SMB1 ROM on CPU by dispatching predecoded basic blocks. It preserves reference CPU behavior, bus/PPU timing and controller polling. CUDA handles the policy and PPO, not NES emulation.

The only current policy input is the full 128×120 luminance screen plus 112 RAM features. Full-screen wide rendering is the default; there is no crop renderer. See [the current Retro guide](../README.md) for the normal commands and configuration.

## Validation

```sh
make -C ocean/retro/batch test
OMP_WAIT_POLICY=PASSIVE python3 tests/test_native_retro_runtime.py ./puffer
```

The differential suite covers instructions, controller polls, serialized emulator state, every rendered pixel, observations, rewards and multiple worker counts. GPU integration tests exercise real PPO updates, graceful interruption saves and checkpoint reload over all 32 starts.

`benchmark_training.py` compares real PPO throughput with compiled/wide/blocking workers against the reference interpreter/renderer with spinning workers, using an explicit compatible checkpoint. Historical results files describe earlier experiments and are not current configuration instructions or current full-screen performance claims.

`python3 ocean/retro/batch/benchmark_cnn.py BASELINE CANDIDATE CHECKPOINT --buffers 1` compares only the
CNN execution implementations in ABBA order. Both runs retain the same model,
checkpoint, environment, and PPO settings. It requires an idle GPU and writes
all outputs to a private temporary directory. `tests/bench_retro_encoder.cu`
provides separate CUDA-graph timings for the encoder itself; those are not
end-to-end training throughput.
The default comparison fixes 64 agents, minibatch 2,048, and replay ratio 2;
override them explicitly with `--agents`, `--minibatch-size`, and
`--replay-ratio`. For example, `--agents 128 --buffers 4 --replay-ratio 1`
benchmarks that configuration without editing `config/retro.ini`.
`python3 tests/test_retro_copy_elision.py BASELINE CANDIDATE CHECKPOINT`
separately checks bit-identical PPO weights after six synchronous updates,
both with and without CUDA graphs, using private checkpoint directories.
See [2026-09-16 CNN results](RESULTS_20260916.md) for the matched measurements,
checkpoint-compatibility checks, and reproduction commands.
The [observation-copy follow-up](RESULTS_20260916_OBSERVATIONS.md) covers the
subsequent borrowed-input and exact AVX2 observation-construction changes.
