# Abyss simulator

## PufferLib 5.0 port

Build with `bash build.sh abyss`, then `./puffer train` (no environment argument).
The simulator remains CPU-based with a GPU learner; `--cpu` builds the standalone
headless evaluator. There was no interactive renderer in the legacy environment.
The port uses typed observations and native inferred masks, without shared-core
changes. Fresh training is the default; request a saved checkpoint explicitly.
The Punisher sweep searches nine bounded dimensions at 100M steps per trial.
See `SWEEP_PUNISHER.md` for ranges, corrected rewards, and the launch command.
Other inherited optimizer/loss dimensions are fixed.

Training samples the empirical T0 templates uniformly and balances 30/50%
Electrical weather. Tests use a frozen legacy mechanics fixture. Reward clipping
at 1 remains enabled; current success/speed/room/loot rewards fit below that
ceiling together, so completion speed is visible to PPO.

This is an early one-second-tick T0/T1 Abyss environment. It consumes final, post-skill
ship statistics rather than reproducing EVE's fitting and skill system. It is a runnable
training substrate, not yet a parity-complete EVE simulation.

NPC local shield/armor repair uses continuous HP/s from exported amount/cycle
attributes. Remote repair supports layer, optimal and falloff with one ally per
healer, but its target selection is an approximation pending live calibration.
All 107 catalog entries preserve repair metadata. Tier 0 uses the 28 recorded
templates; tier 1 samples the 19 Calm encounter families. Both use shared combat
mechanics and the same observation contract; see [tier controls](TIERS.md).

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

`tools/fetch_qsna_spawns.py` preserves the historical snapshot fetcher. It
overwrites snapshot JSON and generated Markdown in `--data-dir`; use a new
directory to inspect a refresh before replacing tracked data. Regenerating
Markdown from the checked-in snapshots is byte-identical. Live API retrieval
has not been requalified by this port.
- `SPEC.md`: sourced weather, cloud, pylon, fit, and action semantics.

Regenerate the compact calibration data with:

```bash
python ocean/abyss/tools/extract_sanderling.py /path/to/events.jsonl
python ocean/abyss/tools/build_npc_catalog.py ocean/abyss/data/npc_stats.csv
python ocean/abyss/tools/calibrate_trajectories.py
python ocean/abyss/tools/build_scenario_catalog.py
python ocean/abyss/tools/build_collider_catalog.py /path/to/registry-live-new-abyss-huge-rocks-detail.txt
```

The recorded QSNA spawn tables are included. The old documentation referenced a
`fetch_qsna_spawns.py` script that was not present in `5c`; regenerating those
external tables is not an implemented workflow in this port.

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
has 1,234 floats and the flattened action mask has 394 entries for both tiers.
Legacy 1,224-input weights can be upgraded with `tools/upgrade_checkpoint.py`;
see [checkpoint migration](TIERS.md#reusing-existing-t0-weights).

At tier 0, the runtime samples one of 28 recorded three-room sequences. It uses the observed hostile
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

T0 and T1 now share this environment and combat mechanics. Select
`--env.filament_tier=0` or `1`. See [tier usage and checkpoint migration](TIERS.md).
