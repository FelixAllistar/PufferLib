# Kaggriculture migration

This is the rule-simulator port, not yet a trainable upstream environment.
There is deliberately no `kaggriculture.h` adapter or training config here yet.
Keep using the preserved old runtime for existing checkpoints and experiments.

`core.h` combines the old simulator's declarations and implementation into one
header, following `SKILL_ISSUES.md` and the header-owned implementation pattern
in `ocean/trianglepath/trianglepath.h`. Its host/device qualifiers follow the
pattern in `ocean/robot_arm/robot_arm.h`. No shared trainer file is changed by
this port. The rule core has no PufferLib, rendering, policy, or reward dependency.

The native rules, structured worker/market actions, seeded daily randomness,
cash/production counters, JSON snapshots, reactive reference bot, and resumable
state layout are preserved. This includes the PLACE-to-shed fix at locked shed
corners and simultaneous market quoting. Reset-bank state format remains version
1 with the same layout; this does **not** establish neural checkpoint compatibility.

The old implicit repair of invalid configuration is replaced by assertions.
Valid settings retain their behavior. Gameplay legality checks still implement
invalid actions as no-ops; snapshot import validation still rejects malformed
input without modifying the destination. Neither is a config-repair fallback.

## Checks

```bash
uv run --no-project --with pytest python -m pytest -q \
    ocean/kaggriculture/tests/test_core.py

# Optional migration comparison against the preserved source checkout:
KAGGRICULTURE_REFERENCE_ROOT=/path/to/old/PufferLib \
    uv run --no-project --with pytest python -m pytest -q \
    ocean/kaggriculture/tests/test_core.py
```

Tests compile optimized and AddressSanitizer/UndefinedBehaviorSanitizer builds.
They independently check market quotes, cow maturity/care, PLACE-to-shed, seed
reservations, neglect, terminal timing, snapshot round trips, and Python's RNG.
The optional old/new comparison checks full-state byte hashes at 69,024
transitions across 96 episodes, plus 2,784 daily JSON snapshots. It covers
random/invalid structured actions and the reactive bot, two resets per seed,
board sizes 4–10, two cash budgets, two shed capacities, weeds on/off, and free
or paid hiring. No GPU work occurs in these tests.

`tests/test_core.c` also contains a CUDA kernel for compile qualification with
NVCC (`-x cu -c`). Compilation alone is not GPU execution/parity qualification.
The existing old-tree `parity.py` can target a shared library built from this
header; it has also passed against the installed Kaggle 1.32.7 interpreter.

## Remaining integration

Port the controller and observation adapter with old/new action/mask parity,
then the entity network and prefix-dependent action sampling/log-prob contract.
Resolve terminal-cash clipping explicitly before training: stock upstream clamps
rewards to `[-1, 1]`. Replay resets, BC/value initialization, checkpoint identity,
external leagues and GPU multi-policy execution remain separate changes.
No old PPO loss, optimizer, eMAG, QD, packed masks, or performance experiment is
included here.

## Source baseline

Preserved `5c` checkout at `3fa5d14cc`, as inspected on 2026-09-23:

- `kaggriculture_core.h` SHA-256:
  `1fe79cff3b3c64d40f301ac308d2c93e2772d0022a71b894ad4daf612e4fa9a8`
- `kaggriculture_core.c` SHA-256:
  `bd221961767f0954983bdc6a798ddee09d914cefe594125f0fdd6dbbc803e150`
