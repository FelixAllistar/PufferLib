# Task controller verification — 2026-09-04

Mode 3 uses the 17 existing unit heads for prioritized mechanical task requests
and leaves the ten market slots under policy control. Mode 2 remains available
for existing checkpoints. The two modes share tensor dimensions but have
different action meanings; their BC datasets are not interchangeable.

Completed checks:

- CPU adapter regression suite passes.
- Task/structured replay label suites: 12 tests pass.
- Portable macro runtime: 20 tests pass.
- Native/portable observation, mask, and repeated-task action parity passes
  across both player views of a rule-generated trajectory.
- One-epoch GPU task BC smoke produced `/tmp/kag_tasks_smoke_256x3.bin`.
- Mode-3 package preflight and four 720-step deterministic smoke games pass.
  This deliberately tiny model stays at $3,000; it is not a trained candidate.

Integration fixes made during verification:

- Mixed learner/frozen action modes require external-only frozen-bank
  sampling. A mode-3 snapshot must not enter a bank decoding mode-2 actions.
- End-of-training reverse-seat evaluations swap action modes with models.
- Native package tests must not import an unbundled repository macro overlay.
  The previous smoke was executing that overlay; its cash figures did not
  measure the packaged task policy. The corrected test asserts overlay absence.
- Adapter test targets now reuse binaries when sources have not changed.

CUDA adapter passes all 12 modes over 1,440 turns, comparing state,
observations, masks, rewards, resets, and logs. The full local GPU trainer
and standalone executable built successfully.

Mixed-mode GPU PPO smoke completed 65,536 steps as
`task_mode3_mixed_smoke_20260904`, saved four checkpoints, and completed
4 games per seat against the existing mode-2 256x3 seed league opponent.
The one-epoch task model lost all eight games with mean cash $0; it is a
runtime integration check, not a competitive candidate or promotion result.
Final implementation diff whitespace checks pass. Existing user reward,
Robocode, and other unrelated changes remain in the working tree.

The next model-quality experiment needs a proper task-labeled replay dataset,
held-out BC fidelity through turn 300, and closed-loop evaluation before any
promotion. Old structured-macro BC files are not suitable task labels.

## Vast reset banks

Uploaded to `root@ssh2.vast.ai:33147` under
`/workspace/PufferLib/ocean/kaggriculture/state_bank/`, with manifests and
summary JSON. All five bank SHA-256 hashes matched the local source:

| Bank | States | SHA-256 |
| --- | ---: | --- |
| full_1365_each.kgb | 21557 | 6f32f1edb1843d99d06846e7171465c366018386e8158cf025f4acfe0c5c221c |
| sell_4096.kgb | 4063 | db7884ceae78b1d509b643cc1aa238e9a09f0180dbb3f57eed65be6790d54b32 |
| market_512_each.kgb | 4056 | 98a03902340455f3c04b9109e268cacec42e3263a0bcb932cc5766f972671e0b |
| maintenance_1365_each.kgb | 4058 | c2bb7a51a00f48448f45ac3744251a51c3ef5b83db8d61fc56f1e9009624abe8 |
| investment_512_each.kgb | 1421 | a57da7d8c3bae9b2f83d50448051a837c2bc66252d5ba89331e6639e6af591e1 |

No remote training configuration was changed for this upload. The banks are
simulator states and can be used by any compatible policy action mode; they
are different artifacts from the task-labeled BC datasets.
