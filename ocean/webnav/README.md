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

All five existing standalone MiniWoB families and the coverage registry are
ported under `families/`; their qualification is documented there. Remaining
policy workflow helpers and the legacy MiniWoB training integration are
separate pending ports; none are replaced by this smaller pilot.
Family builds have their own stricter serialized build rules and must not use
this pilot build command.

## Public DOM observations

The existing DOM-v2 projection and C parser are preserved in `web/dom_snapshot.js`
and `dom.c`. This is a bounded public observation, not a complete browser
accessibility tree: at most 128 nodes, with explicit omitted/truncated counts,
four observation-quality tiers, and opt-in scopes for external widget popups.
It is not yet the converted WebNav DOM training environment or encoder.

After browser setup, run `make -C ocean/webnav dom-test` from the repository
root. `WEBNAV_CHROME` can select an existing browser. The test uses only a
locally constructed page and checks roles, label provenance, whitespace,
hidden-text exclusion, control priority, table relationships, popup scoping,
and malformed-schema rejection. Do not use the unsandboxed CDP launcher on
untrusted sites.

## Text encoder and cache

`make -C ocean/webnav text-test` builds the preserved native Unicode WordPiece
encoder and exact-string vector cache. It requires Clang 19, ICU development
headers/libraries, Node, npm, curl, and SHA-256 tools. Only reference generation
uses Node; runtime encoding remains native C. The script downloads the pinned
Potion tokenizer and embedding table, verifies SHA-256, and installs the pinned
Rust tokenizer Node binding (0.23.2) under ignored `build/webnav/reference/`.

Model: `minishlab/potion-base-8M`, revision
`bf8b056651a2c21b8d2565580b8569da283cab23`. Required non-Git assets:

| File under `build/webnav/reference/` | SHA-256 |
| --- | --- |
| `potion-tokenizer.json` | `e67e803f624fb4d67dea1c730d06e1067e1b14d830e2c2202569e3ef0f70bb50` |
| `potion-model.safetensors` | `f65d0f325faadc1e121c319e2faa41170d3fa07d8c89abd48ca5358d9a223de2` |

Fresh qualification passes 1,120 reference cases with exact token IDs and zero
vector error against the independent normalized-sum reference. Cache tests
check exact Unicode lookup, duplicate IDs, misses, and profile/content mismatch
rejection. Cache files include the ICU major version and must be regenerated
when it changes. These are frozen text features, not a critic or policy model.

Parity is not semantic competence: the preserved 24-case development probe
misses eight examples, including negation, ordering, and translation direction.
The DOM training environment and its learned encoder still need conversion.

## DOM policy adapter: partial conversion

The original `training.c` public-view, legal-action and feature adapter is
preserved, together with the checkpoint-sidecar code. `training_policy.c` now
uses upstream 5.0 CPU inference. This does not register a trainable environment
or add checkpoint hooks to the shared trainer.

`make -C ocean/webnav training-policy-test` needs Clang 19, AVX2/FMA and ICU,
but no Bend runtime or downloaded model. It creates a synthetic H16/L2 test
checkpoint under `build/webnav/training/` with semantic features disabled.
Across 128 recurrent steps and ten resets, actions and all policy/value logits
match legacy CPU inference bit-for-bit (trace `7c4dac1667d333bc`). Twelve negative
cases check source/version/dimension mismatches, invalid architecture settings,
and wrong weight-file sizes. Sanitizer checks pass. This does not establish
GPU parity or learned policy quality.

The sidecar reader checks the compiled source hash as well as the feature and
architecture contract. The standalone test uses the development hash default;
production source-hash generation and checkpoint integration remain pending.
The complete legacy simulator's fresh source build is also still unqualified:
local bounded compilation has exceeded the available memory allowance. Its
models and generators have not been replaced with simplified versions.
