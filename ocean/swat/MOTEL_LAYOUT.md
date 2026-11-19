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

## 3. The site plan as built

Plan hill-climb result (iteration 6, `reports/cloud/motel-layout/LOG.md`),
then adjusted in the engine for gameplay and at street level (section 7).

```
                         rear yard (gravel)                                       z
     +---------------------------------------------------------------------+   -24
     |   fence run (14.5, -14..-8)   tyres, dumpster, transformer           |
     |      +----+            service drive (asphalt)                       |   -13.5
     |      |LDY |                                                          |
     |      +----+----+----+----+----+----+    breezeway                    |
     |      | OF |101 |102 |103 |104 |105 |    x 12..17   (staging 14.1,-3) |   -6..0
     | +---+|    |    |    |    |    |    |               +---+            |
     | |   |+----+----+----+----+----+----+               |   |            |
     | |113| ice = walkway, posts @ 3.0 m ==               |106|            |   0..1.83
     | |   | [pl+picnic] 7 x (2.7 x 5.5) stalls [pl]       |   |            |   1.83..7.33
     | |114|=                                             =|107|            |
     | |115|=  5 stalls    cross aisle 7 m    5 stalls    =|108|            |   7.33..14.33
     | |116|=  2.7 x 5.5   (extraction 0,12)  2.7 x 5.5   =|109|            |
     | |117|   side aisle 7.5 m  [island]  side aisle 8.5  |110|            |   14.33..22.6
     | |118|                     [2.4 m]                   |111|            |
     | |119|--------------------------------------------- -|112|            |   24 (lot edge)
     +-+---+--- verge ---------| driveway |--- SIGN (pole, at the kerb) ----+   29.6 wing ends
                                                                               33..36 kerb/sidewalk
     ============================== road ==================================    36..44
       x: -22  -16 -14.2       -1.2  1.2               15.2  17   23
```

| Element | 5.0 -> after |
| --- | --- |
| Guest rooms | 4 -> 19: rear wing 101-105 (office x -12..-8, rooms x -8..12), east wing 106-112 (x 17..23, z 1.6..29.6, faces west), west wing 113-119 (x -22..-16, z 1.6..29.6, faces east). 101-104 are the authored mission rooms; 105-119 are closed rooms |
| Building footprint | 20 x 12 m strip -> U of 24 + 28 + 28 m wings, 9 % -> 33 % of the lot square |
| Court | open 48 x 48 m asphalt plain -> 29.3 m between walkway edges x 22 m paved (wings run 5.6 m further, to 5 m from the kerb) |
| Stalls | 4.0 m (front row) / 3.0 m (other rows) -> 2.7 x 5.5 m: 7 head-in along the rear wing centred on the parked sedan (one accessible), 5 + 5 along the side wings; corner planters |
| Drive aisles | none -> 7.0 m cross aisle; side aisles 7.5 and 8.5 m either side of a 2.4 x 8.3 m concrete island (paint) |
| Canopy posts | 4.0 m (6 posts) -> 3.0 m along the rear walkway (9 posts: the 6 authored ones, 4 of them moved, and 3 new), 2.8 m along each side wing (11 posts each); same asset and mesh collision |
| Rear / side yard | asphalt plain -> 7 m service drive behind the rooms, gravel beyond and behind the side wings (paint) |
| Wheel stops | 4 m apart (x -10..6) -> in the stall centres x -8.7, -6.0, -3.3, 2.1, 4.8 |
| Sign | (9.7, 8.0) in the lot, facing the building -> pole sign at the kerb (6.4, 33.0), perpendicular to the road, x1.35 (about 5.5 m), with a back-to-back twin |
| Street | road unchanged at z 36..44; the court mouth is now 6 m from the kerb (wing ends) instead of a 34 m apron |
| Staging / extraction | (10.5, -2) against Room 104's east wall / (0, 10) in the open lot -> (14.1, -3) in the east breezeway, behind the building line / (0, 12) in the cross aisle |
| Doors | kept 1.08 x 2.19 m (section 6) |
| Window sill / soffit | unchanged 0.88 / 2.44 m |
| World objects | 1472 -> 1675 (+203 court rows) |

## 4. Authoritative map changes

1. **Court layer** (`motel_court_impl.h`, data `motel_court_data.h` generated
   by `tools/gen_motel_court.py` from `tools/motel_court_spec.py`): 203 rows
   appended after the picnic table, `SWAT_MOTEL_COURT_FIRST`..`_END`;
   `SWAT_MOTEL_OBJECTS` is now the court end and `SWAT_MOTEL_PICNIC_END` is the
   previous canonical prefix (every layer still accepts it). Each closed bay
   repeats Room 102's instance offsets: floor, roof, walkway, canopy, facade,
   rear wall, a **static** leaf in the door opening, window insert, AC sleeve and
   wall lamp; plus end walls, corner piers, walkway posts and the sign twin.
   Every row is a box record with the canonical recipe whose collision is the
   asset's own mesh (`contact_meshes_bind`, like the authored instances),
   `max_health` 0, never a door, room, support or fixture. The closed rooms are
   therefore solid to movement, bullets, sight, sound and navigation (the
   navigation grid queries the same shapes), cannot be breached and hold no
   people. Replicas validate and rebind them (`court_validate`, `court_bind`).
2. **Moved authored data** (`layout_moves` in `motel.c`, same object
   indices; `swat_motel_authored_instance` keeps the 5.0 placement for art):
   walkway posts 115, 116, 117, 119, wheel stops 141-145, sign 140, fence owners 155-158;
   the transformer (2 m back, clear of Room 105) and the picnic table (west
   corner planter) in their layer files.
3. **Mission**: staging and extraction (`mission.c`), see the table.
4. **Protocol / replay**: `SWAT_NET_VERSION` 54 -> 55 (`protocol.h`). The
   five role traces were re-recorded; a build with version 54 still matches
   both annex traces, so only the motel world moved them. Motel thread
   checksum d8582c7b -> 600dd752 (1 and 4 threads); movement checksums unchanged.

## 5. Art layers

| Layer | Change |
| --- | --- |
| motel_v1 kit + materials v3 | court rows draw the same kit models (`motel_court_draw`), so materials v3 binds by file name as for the authored rooms; closed floors are never drawn |
| Exterior kit, architecture overlay | `swat_motel_art_frame`: a court row repeats every exterior-kit and overlay piece of its template owner (Room 102-104 facades, roofs, canopies, rear walls, doors, AC, lamps; office west end 104 or Room 104 east end 109 for the wing ends; post 116), transformed from the template's 5.0 placement; moved owners (posts, sign) carry their own pieces, scaled with the sign. `sections.json` regenerated: instances and sections byte-identical (the kit's inputs did not change, so the kit was not rebuilt), plus a `court` array |
| Windows | closed bays draw the revised guest window with all its drawn covers; no lights inside |
| Lot paint (`ground_decals.png`) | `tools/blender/motel_surroundings.py` paints stalls, accessible stall, island, corner planters, gravel rear/side yards, aisle tyre polish and oil from the spec; backdrop/near-props/road GLBs regenerate byte-identical |
| Surface decals | scatter skips court rows except their walkways (their boxes include trim and openings); the east wall-pack rust moves with the wall pack to Room 105 |
| Props v2 exterior | pickup, wagon and a second van in real stalls, palms in the corner planters and at the lot corners (off the wing ends and the sign), tumbleweed off the walkway |
| Night lights | sign floods follow the sign, east wall pack on Room 105's end wall, porch sconces on 13 of the 15 closed rooms (the 32-light cap) | 
| Props v2 interior | unchanged (rooms 101-104) |

## 6. Doors (kept) and other risks

* **Door leaf 1.08 x 2.19 m kept.** The leaf size is baked into the facade
  opening mesh (`room_facade_4m.glb`), the door recipe in `motel.c` (half
  extents .54/1.095), Room 101's hero breach assemblies and hinge plates
  (`check_room101_hinges`, `swat_motel_hinge_frame_owner`), the door art meshes
  and the door-frame, door-support and breach-geometry tests. A 0.95 x 2.08 m
  leaf needs a new facade mesh with a smaller opening and new Room 101
  hardware, which the brief rules out ("do not change Room 101's interior");
  the closed rooms reuse the same leaf so every door on the site matches.
* **Longer approach.** Staging is 3.6 m further east and every squad route
  enters through the breezeway, so tests with tick budgets tied to the 5.0
  distance were given the extra time (documented in each test).
* **Presentation-only vehicles** (props v2) have no collision; they are
  parked in side-row stalls away from the evacuation lines.
