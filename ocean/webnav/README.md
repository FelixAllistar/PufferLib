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

The pilot evaluator uses upstream CPU inference and the preserved browser
page/CDP transport. From the repository root:

```sh
make -C ocean/webnav evaluator
bash ocean/webnav/setup_browser.sh
make -C ocean/webnav browser-test
build/webnav/evaluate native 300 - 128 2 100000 expert
# Supply the checkpoint's actual hidden size and layer count:
build/webnav/evaluate browser 100 checkpoint.bin 128 2
```

`WEBNAV_CHROME` can point to an existing browser executable. The setup script
pins Chrome for Testing 153.0.8010.52 and verifies the downloaded archive's
SHA-256. `watch` replaces `browser` for graphical playback, with a headed
browser installed using `setup_browser.sh headed`; visual playback is not yet
qualified in this conversion. The CDP launcher uses `--no-sandbox`: these
commands are intended for the repository's trusted local pilot page only.

Qualification: 300 mixed and 90 expert browser episodes plus six scripted
edge cases pass observation/outcome conformance against Bend. The expert
solves 300/300 native episodes. The new H32/L1 smoke checkpoint passes browser
conformance for 32 episodes; its deterministic native action/state traces
match legacy CPU inference byte-for-byte across 300 episodes. This does not
establish floating-point logit parity or trained policy quality.

Remaining policy workflow helpers, MiniWoB families and WebNav DOM remain
separate pending ports; none are replaced by this smaller pilot.
Family builds have their own stricter serialized build rules and must not use
this pilot build command.
