# Abyss simulator

This is an early one-second-tick T0 Abyss environment. It consumes final, post-skill
ship statistics rather than reproducing EVE's fitting and skill system. It is a runnable
training substrate, not yet a parity-complete EVE simulation.

Data sources:

- `data/npc_stats.csv`: original 107-row NPC export.
- `data/npc_catalog.json`: normalized NPC definitions.
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
```

Build the environment with `bash build.sh abyss`. Dark weather, the modern turret hit
equation, and targeting time are represented. The current combat constants marked
provisional in `config/abyss.ini` still need exact fitted module values.

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
