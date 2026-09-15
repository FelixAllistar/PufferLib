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
