# WebNav pilot: conversion status

This is the original three-task pilot, not the full MiniWoB/browser suite.
Game behavior remains in stock CPU Bend 2.0.6; C transports the batch and
projects public observations. `contract.json` defines the preserved interface:
32 lanes, 640 float features, 13 masked actions, and a 16-action episode limit.

## Build and test

Requires stock Bend 2.0.6 (`BEND_BIN`, default `$HOME/.bend/bin/bend`),
Clang 19, and the repository's Raylib headers. From the repository root:

```sh
systemd-run --user --scope -p MemoryMax=4G -p MemorySwapMax=0 \
    -p CPUQuota=100% -p TasksMax=128 \
    timeout --signal=KILL 300 bash ocean/webnav/build.sh
make -C ocean/webnav test
```

The resource cap must fit available host memory. Generated C, the static
bridge library, compiler version and source hashes go under `build/webnav/`.
The bridge must be rebuilt after changes to Bend or bridge sources.

On 2026-09-24, the capped build and both tests passed: 102,400 transitions
against an independent specification with four concurrent callers, followed
by adapter checks for legal masks, terminal rewards and automatic resets.
These tests do not establish browser parity or learned policy quality.

## Native training

After building the bridge, `bash build.sh webnav` links the CPU simulator to
the GPU learner. The environment uses the existing vector initializer hook;
total agents must be divisible by `32 * num_buffers`. The trainer supplies
the 13-entry action masks through its existing CPU mask interface. No custom
loss, optimizer or shared trainer changes are needed. The Bend runtime
serializes calls, so additional CPU workers do not parallelize Bend execution.

The original H128/L2 pilot config is retained, starting fresh rather than
loading an unrelated checkpoint. An SM61 FP32 qualification completed 4,096
training steps with H32/L1, 64 agents, two buffers, async and CUDA graphs,
finite losses, episode metrics and a saved checkpoint. This is a training
integration smoke test, not evidence of task mastery or production throughput.

## Remaining conversion

Browser evaluation, policy tools, MiniWoB families and WebNav DOM remain
separate pending ports; none are replaced by this smaller pilot.
Family builds have their own stricter serialized build rules and must not use
this pilot build command.
