# Abyss T0 → T1 handoff: data flow + QSNA spawn data

Date: 2026-09-28. Scope: how live EVE data becomes the sim, what the new
QSNA/T1 data is, where it came from, and what is still T0-only.

## Tier naming

- T0 = Tranquil filament.
- T1 = Calm filament.
- QSNA tier order: `tranquil, calm, agitated, fierce, raging, chaotic, cataclysmic`.

## Current data flow (T0 sim)

Live EVE client → Sanderling memory reader → recorded sessions → compact
sim tables → C headers compiled into `abyss.h`.

1. `C:\Users\sunde\projects\Sanderling` reads the live client read-only:
   `implement/read-memory-64-bit`, `recorder/` sessions (`events.jsonl`,
   `rooms/`, UI snapshots), plus controller/executor work.
2. `tools/extract_sanderling.py /path/to/events.jsonl`
   → `data/recorded/episodes.json`: 28 runs / 84 rooms with initial layouts.
   → `data/recorded/frames.jsonl`: raw native-vector calibration input.
   This file is gitignored (`ocean/abyss/.gitignore`).
3. `tools/build_npc_catalog.py ocean/abyss/data/npc_stats.csv`
   → `data/npc_catalog.json`: normalized T0 NPC definitions.
4. `tools/calibrate_trajectories.py`
   → `data/trajectory_calibration.json`: pursuit/orbit fits.
5. `tools/build_scenario_catalog.py`
   → `generated_scenarios.h`: C NPC defs + rooms.
6. `tools/build_collider_catalog.py /path/to/registry-live-new-abyss-huge-rocks-detail.txt`
   → `generated_colliders.h`: rock colliders.
7. `tools/fetch_qsna_spawns.py`
   → QSNA snapshot + readable tables (see below).

Runtime: `abyss.h` is a one-second-tick T0 Abyss env using final post-skill
fit stats (`SHIP_PROFILE.md`, `SPEC.md`). It samples one of the 28 recorded
three-room sequences: observed hostile comps, NPC stats, cache/conduit XYZ,
hostile XYZ, pylon XYZ. Policy ABI: 64 room-stable slots, 1,224-float obs,
394-entry mask. CPU env + GPU learner (`README.md`).

Regenerate calibration:

```bash
python ocean/abyss/tools/extract_sanderling.py /path/to/events.jsonl
python ocean/abyss/tools/build_npc_catalog.py ocean/abyss/data/npc_stats.csv
python ocean/abyss/tools/calibrate_trajectories.py
python ocean/abyss/tools/build_scenario_catalog.py
python ocean/abyss/tools/build_collider_catalog.py /path/to/registry-live-new-abyss-huge-rocks-detail.txt
python ocean/abyss/tools/fetch_qsna_spawns.py
```

## New QSNA / abyssal.space data (already fetched, not yet wired into sim)

Source: qsna.eu frontend for abyssal.space player-submitted runs. This was
scraped from the React bundle API endpoints, in two requests:

- `https://www.qsna.eu/api/eve/custom/abyss/stats`
- `https://www.qsna.eu/api/eve/universe/items/npcs/1982;1997`

Fetcher: `tools/fetch_qsna_spawns.py`. Outputs:

- `data/spawn_stats_qsna.json`: per-tier spawn probability tables.
- `data/npc_types_qsna.json`: 120 Abyss NPC types with dogma attributes.
- `data/spawn_tables_qsna.md`: generated readable version, do not hand-edit.

Semantics: `roomsCount` = sample size; per-spawn `average/min/maxEnemiesPerRoom`;
per NPC type `chance/min/max/avg` = per-room occurrence frequency. Spawn tables
are weather-independent: weather changes attributes, not which comps roll.

## T1 = calm: what the data says

T1 is **not implemented** in the sim. `generated_scenarios.h` still comes only
from the 28 T0 empirical episodes.

QSNA contrast (`data/spawn_tables_qsna.md`):

- T0 `tranquil`: 9 spawn types with samples; rooms are small.
  Examples: `droneSwarm` rooms=232 avg=2 min=1 max=3;
  `triglavianGangs` rooms=151 avg=1 x1-1;
  `concordFleet` 70% Attacker Pacifier / 28% Skybreaker;
  several T1 spawns have “no T-data samples”.
- T1 `calm`: 18 spawn types with samples; rooms are larger and mixed.
  Examples: `droneSwarm` rooms=188 avg=6 min=4 max=8;
  `triglavianGangs` rooms=104 avg=3;
  `triglavianAndRougeDrones` rooms=38 avg=4 min=4 max=5;
  new sampled spawns include `damagedDrifterBsAndSeekers` (61 rooms),
  `damagedDrifterFleet` (62), `droneBattlecruisers` Tesseras (67),
  `droneBoss` Overmind + escort (56), `rodivaKikimoras` Rodiva + Damavik (75),
  `damagedTriglavianBattleships` Leshak (56), `triglavianBattlecruiser`
  Drekavac (24), plus larger sleeper/seeker/drifter mixes.

Your 28 T0 Dark captures covered 8 of 9 sampled T0 spawns; the miss was rare
`triglavianDroneFleet` Vila swarm (5 rooms in QSNA sample).

## T1 blockers / next work

1. `tools/build_scenario_catalog.py` only samples empirical T0 episodes and
   `GeneratedRoom` caps `hostiles[3]`; T1 needs chance-weighted sampling from
   `spawn_stats_qsna.json` and larger room support (calm max 8).
2. Extend NPC coverage from `npc_catalog.json` toward `npc_types_qsna.json`
   for T1-only types (Leshak, Overmind, Tesseras, Rodiva/Kikimora, Drekavac,
   drifter mixes).
3. Keep T1 weather penalty rolls consistent with `SPEC.md`
   (tranquil–fierce 30/50%; raging+ 50/70%).
4. Live weapon/weather sensing is separate Sanderling work; this handoff only
   covers sim data provenance.
