# Briar Court site layout: a real motor court at human scale

Owner feedback: the motel level is "super small and weird". The art passes on
5.0 fixed materials and detail but not the site. This document is the plan
and the record for the layout pass on `swat/motel-layout`. Hill-climb log,
references and contact sheets: `reports/cloud/motel-layout/`.

Engine metres throughout: +x east, +z toward the street (road z 36..44),
ground top y = -0.08, room floors y = 0. "Object N" is a world object index
(`tag.index`); motel instance i is object i+1.

## 1. Survey: what the map is today (5.0)

Measured with `tools/motel_site_dump.c` (`make motel-site-dump`), plan render
`tools/motel_site_plan.py` (`reports/cloud/motel-layout/plan_sheet_ref_before_after.jpg`, middle).

| Item | Today | Real motor court (references) |
| --- | --- | --- |
| Building | one strip x -12..8 (20 m), office + 4 guest rooms (101-104), laundry annex behind the office | 10-24 rooms in an L, U or long strip wrapped round the parking |
| Room bay | 4.0 m wide, 6.0 m deep, 2.8 m walls | 3.6-4.3 m x 6-8 m: fine |
| Lot | 48 x 48 m asphalt slab (object 0, x -24..24, z -24..24); building covers 9% of it | lot hugs the building; the court is the parking |
| Stalls (paint) | front row 4.0 m wide x 5.5 m, other rows 3.0 m | 2.6-2.75 m x 5.5 m |
| Drive aisle | undefined (open plain) | 7 m two-way |
| Canopy posts | 4.0 m apart (6 posts, objects 114-119) | 2.5-3 m |
| Door leaf | 1.08 x 2.19 m | 0.91-0.97 x 2.03-2.13 m |
| Window sill / canopy soffit | 0.88 m / 2.44 m | fine |
| Street setback | building front (z 1.83) to road edge (z 36): 34 m of empty asphalt and dirt | court opens to the street; office/sign at the street |
| Sign | object 140 at (9.7, 8.0), inside the lot | at the street |

What is anchored where (everything that must follow a layout change):

* Authoritative map (`motel.c` `swat_motel_build`, replicated and validated
  by `swat_motel_bind_collision` on every client): base instances
  (`motel_data.h`, objects 1-146), utility (147-158, fence owners 155-158),
  framed party walls 106-108, sectioned wall skins, fence pieces (957-1043),
  surroundings banks/rocks (1044-1118, `motel_surroundings_data.h`), ground
  supports (1119-1172, `motel_ground_data.h`, hash-checked GLB handoff),
  mounted/props/panes/nightstand/windows/seating/bag/cabinet/service/
  screwdriver/dumpster/sedan/shelter/transformer/picnic (1173-1471). Every
  layer validates its exact object count, so a new layer appends after them.
* Mission (`mission.c`): staging (10.5, -0.079, -2) against Room 104's blind
  east wall; extraction (0, -0.079, 10); squad spawns are staging offsets.
* Presentation layers: exterior kit (`motel_exterior_data.h`, per owner),
  architecture overlay (`motel_exterior_arch_data.h`, per owner and per
  wall-skin section centre, built from `sections.json`), surroundings
  backdrop/near props and `ground_decals.png` (lot paint, `tools/blender/motel_surroundings.py`),
  decals (`assets/environment/motel_decals/placement.txt`), props v2 exterior
  (`motel_props_ext_v2_data.h` from `placements.json`) and interior (rooms only),
  materials v3 (bound by GLB file name, so reused assets inherit it).

## 2. References (all public domain, US government works)

| Use | Reference | Source |
| --- | --- | --- |
| Site plan (hill-climb target) | Luna Lodge, Albuquerque NM: existing site plan, HAER NM-13 sheet 4 | https://www.loc.gov/item/nm0300/ (HAER, Library of Congress) |
| Massing / growth | Luna Lodge 2006 axonometric and 1949-1954 evolution, HAER NM-13 sheets 2-3 | same |
| Street level (court) | Luna Lodge court view, HAER NM-13 photo 19 | same |
| Street level (street) | Luna Lodge from Central Ave, HAER NM-13 photo 13 | same |
| Site plan (cabin court) | Lilly's Auto Camp, Gilroy CA, HABS CA-2094 sheet 1 | https://www.loc.gov/item/ca0920/ |

Luna Lodge is the textbook Route 66 U court: two long room wings
perpendicular to the street, joined by a short rear wing, office and sign at
the street, angled parking along both wings and a long concrete island down
the middle; court about 24 m between the wing fronts, 28 rentable rooms.

## 3. The new site plan (plan hill-climb result, iteration 6)

```
                         rear service yard (gravel)                    z
        +--------------------------------------------------------+   -24
        |      laundry                                           |
        |      +----+  dumpster, transformer (service drive)     |   -13.5
        |      |    |                                            |
 west   | +---++----+----+----+----+----+----+----+    breeze-   |
 wing   | |113|| OF | 101| 102| 103| 104| 105| 106|    way (stg) |   -6..0
 113-119| |   |+----+----+----+----+----+----+----+-------+      |
        | +---+ ice ===== covered walkway, posts @2.9 m ===+------+   0..1.83
        | |114|  [pl] | | | | |#| | | | | [pl]|        | 107|   |
        | +---+       rear row 7 x 2.7 m stalls          +----+   |   1.83..7.33
        | |115| ===                                   === | 108|   |
        | +---+ ===     cross aisle 7 m      (extract)  === +----+   |   7.33..14.33
        | |116| ===   side rows                       === | 109|   |
        | +---+ ===   5 x 2.7 m      [island]    5 x 2.7 === +----+   |
        | |117| ===        aisle 7 m |    | aisle 7 m   === | 110|   |
        | +---+                      |    |                 +----+   |
        | |118|                      +----+                 | 111|   |   22.6
        | +---+-------------------------------------------- +----+   |   24 (lot edge)
        | |119|  walkways continue      driveway        |112 |      |   25.83
        +----------------------------| |---- SIGN -------------+      |
             sidewalk, bus shelter    | |                                34.9..36
        ================================ road ==========================   36..44
```

| Element | Dimensions (before -> after) |
| --- | --- |
| Guest rooms | 4 -> 19 (4 authored + 15 closed): rear wing 101-106 (x -12..16), east wing 107-112 (x 16..22, z 1.83..25.83, faces west), west wing 113-119 (x -22..-16, z -2.17..25.83, faces east) |
| Court | open 48 x 48 m plain -> 28.3 m (between walkway edges) x 22-24 m, open to the street |
| Stalls | 4.0 / 3.0 m wide -> 2.7 x 5.5 m: 7 head-in along the rear wing (centred on the parked sedan), 5 + 5 along the side wings |
| Drive aisles | none -> 7.0 m cross aisle and two 7.0 m side aisles round a 2.4 x 8.3 m concrete island |
| Canopy posts | 4.0 m (6) -> rear walkway 10 posts at 2.91 m, east wing 8 more at 3.0 m (its corner post is the rear run's last), west wing 11 at 2.8 m (6 existing posts moved, 23 new, same asset and mesh collision) |
| Rear yard | 14 m of asphalt -> 7 m service drive behind the rooms, gravel beyond (paint) |
| Sign | (9.7, 8.0) in the lot -> (6.6, 31.6) at the street beside the driveway |
| Street | road stays at z 36..44 (hash-checked ground handoff); the wings now run to z 25.8, so the court mouth is 10 m from the kerb instead of a 34 m apron |
| Doors | kept 1.08 x 2.19 m (see risks) |

Plan iterations, scores and what changed: `reports/cloud/motel-layout/LOG.md`.

## 4. What changes in the authoritative map

1. **New court layer** appended after the picnic table
   (`SWAT_MOTEL_COURT_FIRST = 1472`, `SWAT_MOTEL_OBJECTS` grows to the court
   end; the old meaning becomes `SWAT_MOTEL_PICNIC_END`). Instances generated by
   `tools/gen_motel_court.py` from `tools/motel_court_spec.py` into
   `motel_court_data.h`. Each closed bay reuses the motel_v1 room kit with the
   same instance offsets as Room 102: facade, rear wall, roof, floor, walkway,
   canopy, a **static** (non-door) locked leaf, window insert, AC sleeve, wall
   lamp, plus end walls between bays and the posts. Every piece uses the
   asset's own mesh collision (like the base instances), so the closed rooms
   are solid to movement, bullets, sight and navigation; no room records, no
   occupants, no interior content.
2. **Moved base data** (same object indices, new recipe): canopy posts
   114-119, wheel stops 141-145 into stall centres, sign 140, fence owners
   155-158 (to the east property line x 23.2), picnic table (west corner planter).
3. **Mission**: staging moves to the east breezeway (19, -3), behind the
   building line between Room 106 and the east wing (no occupied door or window
   sees it); squad spawn offsets unchanged relative to staging. Extraction
   stays at (0, 10) in the cross aisle.
4. **Protocol/replay**: the replicated map changes (object count and
   recipes), so `SWAT_NET_VERSION` is bumped and the determinism traces are
   re-recorded (`make determinism-test` hashes; motel thread checksum).

## 5. What moves in each art layer

| Layer | Change |
| --- | --- |
| Exterior kit (gutters, lamps, meters...) | court pieces drawn by cloning the per-owner placements of the template bay (Room 102's owners) onto each closed bay transform; moved posts carry their post-base pieces |
| Architecture overlay | same cloning for owner pieces (roof, canopy, posts) and for the template facade's per-section pieces (parapet, surrounds, collars, bull-nose awning), so the new wings match the strip; `sections.json` regenerated |
| Windows | closed bays draw the revised window model with its drawn curtains; no light inside |
| Ground decals (`ground_decals.png`) | stall paint redrawn from the spec (2.7 m), island, rear-yard and side gravel, oil stains in the new stalls; old 4 m / 3 m rows removed |
| Surroundings / near props | lot curbs and pole lights follow the court; sign moved |
| Props v2 exterior | vehicles moved into real stalls; verge props clear of the wings |
| Decals (placement.txt) | checked against the new walls (no decal floating on the old lot plain) |
| Materials v3 | no change (bound by asset file; court reuses the same assets) |
| Props v2 interior | no change (rooms 101-104 untouched) |

## 6. Risks

* **Door leaf size**: the 1.08 x 2.19 m leaf is baked into the facade
  opening mesh, the door recipe (`motel.c` half extents .54/1.095), the
  Room 101 hero breach assemblies and hinge plates (`check_room101_hinges`),
  door-frame, door-support and breach tests, and the door art meshes. Resizing
  it means a new facade mesh with a smaller opening and new Room 101 hardware,
  which the brief forbids ("do not change Room 101's interior"); kept, and the
  closed rooms reuse the same leaf so every door on the site matches.
* **Squad routes**: staging moves; any test that counts ticks from staging
  to a door may need the new distance. Mitigation: staging stays within 9 m of
  Room 104's door, entries via the east breezeway and the walkway.
* **Object budget**: about 1700 of 2048 objects; breach and rubble use their
  own capacity.
* **Frame cost**: ~15 extra room bays of the same kit; measured with
  `performance_tool` (software GL) before/after.
* **Road position**: moving the highway would re-cut the hash-checked ground
  supports, surroundings banks, backdrop, road paint and bus shelter; not done.
