# Legacy 2/1 replay coverage, 2026-09-29

CPU-only diagnostic; no trainer, reward, config, or remote changes.

Replay: archived Majkel1337 episode 110907373, seat 1, September 19,
Kaggle module 1.32.7. Final cash 156278 versus 154751. This was selected as
the highest-cash Majkel game in the preserved `majkel_64/catalog.tsv`, not
as a representative random sample or verified current leaderboard champion.

Both legacy and current simulators reproduce all 720 official frames and final
cash exactly. There are 719 transitions: frame zero's action is not a transition.
The earlier command-only scan counted 720 action records; use these aligned
native-effect results instead.

## Method

Legacy source: git commit `6abfb8dd4`, explicit executor version 1. Enumerate
all legal macros 0..36, eight quantities, and available quadrant choices at
each original expert state: 405040 candidates total. Use the maximum legacy
worker limit (240); this replay uses at most 11 hands, so it does not bind.

Apply worker commands to a state copy using native atomic-plant validation and
worker ordering. Count successful PLANT, BUILD_COOP, BUILD_PASTURE, DIG,
FERTILIZE and PLACE tile effects by operation, species and quadrant. Ignore
worker identities. Prefer maximum matched effects, then minimum extra effects.
Count whether *any* candidate matches the full effect vector exactly.

Current 2/2 uses the existing BC projector and native decoder, NOT exhaustive
search. Measure its decoded effects with the identical metric at identical
parity-checked states. Thus legacy gets an exhaustive oracle while current
gets a constructive lower bound from an existing projector.

The real expert action pair always advances the replay. Neither controller
is rolled out closed-loop. These are per-state representability measurements.

## Results

| Metric | Legacy 2/1 exhaustive | Current 2/2 projection |
|---|---:|---:|
| Matched strategic effects / 447 | 269 (60.2%) | 444 (99.3%) |
| Exactly matched active turns / 285 | 143 (50.2%) | 284 (99.6%) |
| Extra strategic effects in selected outputs | 4 | 0 |

Legacy phase breakdown, zero-based transition indices:

| Transitions | Matched effects |
|---|---:|
| 0..239 | 67 / 75 (89.3%) |
| 240..479 | 99 / 158 (62.7%) |
| 480..718 | 103 / 214 (48.1%) |

Current's only mismatch is transition 562: six observed region/type requests
(three fertilizer and three crop-harvest groups) exceed five request slots.
The projector cannot compact them exactly and leaves production unlabeled.
The three successful fertilization effects are therefore missed. This is a
projector/slot-cap observation, not proof that no alternative current macro
request could perform those fertilizer effects while changing the harvests.

Legacy can match strategic work AND the exact market command queue on 300/719
transitions. This is a separate command metric, not market-fill/economic
equivalence; do not merge it with the effect-coverage percentage.

Sanity gate: at 24 evenly spaced replay states, decode a selected request using
the legacy production context API, then search for that decoded action. All 24
are recovered exactly on strategic effects AND market commands.

## Interpretation and unfinished economic test

This establishes a real same-turn capacity gap, particularly late-game. It
does NOT establish that 2/1 cannot achieve high cash by choosing a different
schedule. Harvesting, maintenance, movement efficiency, and market fills are
not included in the 447-effect denominator. Neither percentage represents
whole-game strategic equivalence or learned-policy performance.

A closed-loop economic comparison remains undone. It needs an adaptive expert
or an explicitly specified strategy-following controller for the changed
states. Replaying the original opponent/teacher primitive tapes after state
divergence is not a valid adaptive strategy test. A newly written greedy
controller's failure would also not establish a 2/1 upper bound.

## Reproduction

From repository root, use a disposable build-artifact directory, not another
installation or worktree:

```sh
mkdir -p build/kaggriculture/exec21_audit_20260929/source
git archive 6abfb8dd4 ocean/kaggriculture src | tar -x -C build/kaggriculture/exec21_audit_20260929/source
cc -O2 -shared -fPIC -mavx2 -mfma \
  -Ibuild/kaggriculture/exec21_audit_20260929/source/src \
  -Ibuild/kaggriculture/exec21_audit_20260929/source/ocean/kaggriculture \
  -Iraylib-5.5_linux_amd64/include \
  ocean/kaggriculture/tests/audit_exec21.c \
  raylib-5.5_linux_amd64/lib/libraylib.a -lm -ldl -lpthread \
  -o build/kaggriculture/exec21_audit_20260929/audit.so
make -C ocean/kaggriculture replay-bridge BUILD="$PWD/build/kaggriculture/exec21_audit_20260929/current"
uv run --no-project --with numpy python ocean/kaggriculture/tests/audit_exec21.py \
  --archive /home/felix/puffertank/elite_replays/raw/kaggriculture-episodes-2026-09-19/kaggriculture-episodes-2026-09-19.zip \
  --episode 110907373 --seat 1 \
  --lib build/kaggriculture/exec21_audit_20260929/audit.so \
  --current-lib build/kaggriculture/exec21_audit_20260929/current/kag_bc_replay.so \
  --output build/kaggriculture/exec21_audit_20260929/repeat_comparison.json
```

Outputs are exclusive: use a fresh report filename. Per-transition results
from the first comparison are in
`build/kaggriculture/exec21_audit_20260929/110907373_comparison.json`.
