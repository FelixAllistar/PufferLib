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

## Remaining conversion

Native 5.0 training is explicitly disabled until the 32-lane batch and action
mask integration is ported. The legacy configure hook is not consumed by
upstream 5.0. Browser evaluation, policy tools, MiniWoB families and WebNav DOM
remain separate pending ports; none are replaced by this smaller pilot.
Family builds have their own stricter serialized build rules and must not use
this pilot build command.
