# Abyss simulator

This is an early one-second-tick T0 Abyss environment. It consumes final, post-skill
ship statistics rather than reproducing EVE's fitting and skill system. It is a runnable
training substrate, not yet a parity-complete EVE simulation.

Data sources:

- `data/npc_stats.csv`: original 107-row NPC export.
- `data/npc_catalog.json`: normalized NPC definitions.
- `data/spawn_stats_qsna.json`: QSNA/abyssal.space per-tier historical spawn
  probability tables for all seven tiers and 21 spawn types.
- `data/npc_types_qsna.json`: QSNA NPC catalog with dogma attributes for the
  Abyss NPC groups (superset of the T0 catalog, covers T1+ NPCs).
- `data/spawn_tables_qsna.md`: generated, human-readable spawn tables.
- `data/recorded/episodes.json`: 28 runs / 84 rooms and initial layouts.
- `data/recorded/frames.jsonl`: local raw native-vector calibration input (gitignored).
- `data/trajectory_calibration.json`: robust, speed-bounded pursuit/orbit fits.
- `SPEC.md`: sourced weather, cloud, pylon, fit, and action semantics.

Regenerate the compact calibration data with:

```bash
python ocean/abyss/tools/extract_sanderling.py /path/to/events.jsonl
python ocean/abyss/tools/build_npc_catalog.py ocean/abyss/data/npc_stats.csv
python ocean/abyss/tools/calibrate_trajectories.py
python ocean/abyss/tools/build_scenario_catalog.py
python ocean/abyss/tools/build_collider_catalog.py /path/to/registry-live-new-abyss-huge-rocks-detail.txt
python ocean/abyss/tools/fetch_qsna_spawns.py
```

Build the environment with `bash build.sh abyss`. Dark weather, the modern turret hit
equation, targeting time, layer resistances, nonlinear capacitor recharge, cycle-based
module capacitor costs, MWD signature bloom, and start/end local-repair timing are
represented. `SHIP_PROFILE.md` documents the resolved fit values required for another ship.

The policy ABI uses 64 randomized, room-stable entity slots. Overview ordering is not
part of identity: an NPC, cache, conduit, or tower keeps its slot until the room changes.
Destroyed NPC slots become absent vectors without being reused; the cache remains observable
as its wreck. Six independently masked heads select navigation
`hold|stop|approach(slot)`, targeting `hold|lock(slot)|focus(slot)`, weapon
`off|fire(slot)`, propulsion/repair `off|on`, and interaction
`hold|loot|open(slot)|activate(slot)`. The three module heads declare their complete
desired state every tick. `fire(slot)` is reconciled as a persistent retargeting transaction:
stop the old cycle, focus the requested target, then start the weapon on a later tick.
`open(slot)` and `activate(slot)` are also persistent transactions: one command starts
automatic approach and completes at cargo or conduit range, matching EVE's default action.
Only one pointer operation lands at the end of each tick, after the old world has advanced,
prioritized as open/activate, weapon focus, explicit targeting, then navigation. Loot is
the non-pointer Enter shortcut and can coexist with that pointer operation. This matches
the measured live proposal-to-landing median of about 0.95 seconds. There is no
nearest-hostile shortcut. The observation
has 1,224 floats and the flattened action mask has 394 entries. Earlier Abyss checkpoints
are incompatible and must be retrained.

The runtime samples one of 28 recorded three-room sequences. It uses the observed hostile
compositions, NPC catalog statistics, cache/conduit XYZ, hostile XYZ, and named support-pylon
XYZ. Clouds use randomized unions of two to four oriented ellipsoids because their native
geometry was not identified reliably.
Giant-rock rooms sample one of two measured 30-sphere overlapping unions from the saved
native-ball capture. The nearest eight sphere clearances are exposed to the policy and
both player and NPC motion resolve against the union.

Required parity work remains: improve NPC steering calibration, calibrate randomized cloud
frequency/geometry, and run replay drift checks. The sphere-union obstacle colliders and
nearest-clearance observations are implemented. Until replay checks pass, policies from
this environment should not be treated as parity-complete sim-to-real candidates.

Native ABI smoke tests are available with:

```bash
make -C ocean/abyss test
make -C ocean/abyss sanitize
```
