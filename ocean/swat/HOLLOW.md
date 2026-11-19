# The Hollow (`--mission hollow`)

A suspense raid on a rural property at the end of a gravel lane in a West
Virginia hollow. An old two-storey **boxed house**, a grey board barn, a
smokehouse, a root cellar dug into the hill, junk in the yard and woods on
every side. Dusk, overcast, fog under the trees, one bare bulb burning in the
house and one in the cellar.

The horror is implied: drag marks, dried stains, a padlocked freezer, a tarp
over a shape, too many shoes, a wall of flyers. No gore detail, no real people,
no real cases. Every stain and mark is a stylised decal made here.

Status and the per-item hill-climb log: `reports/cloud/level-hollow/LOG.md`.

## 1. Building type, confirmed from photographs

The owner described "a big old, almost like a barn house, wood house thing ...
not a log cabin or a normal wooden house, just boards nailed together". The
photographs below confirm this is the Appalachian **boxed house** (box house,
plank house, sometimes called a "single-wall" house):

- The walls are **wide vertical rough-sawn boards** (25-35 cm), nailed to a
  heavy sill at the bottom and a plate at the top.
- There is **no stud frame**, so the wall is one board thick (about 2.5 cm).
  Narrow **battens** cover the joints, or the joints show gaps.
- Boards are unpainted silver-grey, darkening to brown at the bottom where rain
  splashes. The rare painted ones are flaking.
- The house is usually two storeys, a hall-and-parlor or I-house plan one room
  deep, under a side-gable roof.
- A big field-stone **exterior chimney** stands on one gable end.
- A full-width porch sits on stone piers, with a shed roof of rusted
  standing-seam or corrugated tin.
- Small wooden-sash windows (6/6 or 2/2), often broken or boarded.
- A lean-to kitchen is tacked onto the back.

Wolcott's 1938 farmhouse (below) is exactly this: vertical boards with gaps, a
shed porch roof, small sash windows and a slumping stone chimney on the gable.
The Evans 1935 shed and the Highsmith barns show the same board wall on
outbuildings, and give the colour and value of weathered boards and rusted tin.

| Reference (committed copy, <= 1024 px) | Use | Source / author / licence |
|---|---|---|
| `references/house_wolcott_1938_a.jpg` | **Main house** silhouette, boards, chimney | "Old abandoned farmhouse on main highway between Morgantown and Elkins, West Virginia", Marion Post Wolcott, Sept 1938, FSA/OWI. https://www.loc.gov/pictures/item/2017753393/. Public domain (US government work, no known restrictions). |
| `references/house_wolcott_1938_b.jpg` | Second view of the main house, tree and yard | Same series, https://www.loc.gov/pictures/item/2017753392/. Public domain. |
| `references/house_wolcott_1938_boards.jpg` | Board-and-gap wall, door hood, ladder, sash detail | Same series, https://www.loc.gov/pictures/item/2017753383/. Public domain. |
| `references/site_hollow_wolcott_1938.jpg` | Site: a board house in a clearing in a wooded hollow, rail fence | "Abandoned farm. West Virginia", Marion Post Wolcott, 1938, FSA/OWI. https://www.loc.gov/pictures/item/2017799290/. Public domain. |
| `references/board_shed_evans_1935.jpg` | Vertical-board outbuilding, tin roof, tool shed | "Roadside barn. Monongalia County, West Virginia", Walker Evans, June 1935, FSA. https://www.loc.gov/pictures/item/2017759147/. Public domain. |
| `references/barn_monroe_highsmith_2015.jpg` | **Barn**, plus colour and value of grey boards and a rusted red tin roof | "Old barn in Monroe County, West Virginia", Carol M. Highsmith, 2015. https://www.loc.gov/pictures/item/2015634364/. Public domain (Highsmith gift, no known restrictions). |
| `references/barn_sagging_highsmith_2015.jpg` | Barn wear: sagging ridge, rusted corrugated roof | "A sagging barn in Jefferson County, West Virginia", Carol M. Highsmith, 2015. https://www.loc.gov/pictures/item/2015631435/. Public domain. |
| `references/smokehouse_crow_farm_habs.jpg` | **Smokehouse** | HABS WV-0490, Jacob Crow Farm, Smoke House, Cameron, Marshall County, WV. William E. Barrett, 1991. https://www.loc.gov/pictures/item/wv0490.photos.373283p/. Public domain. |
| `references/root_cellar_door_habs.jpg` | **Root cellar** entry: plank door set into a bank | HABS NE-0068, Hans Ehlers Farm, Root Cellar. https://www.loc.gov/pictures/item/ne0068.photos.103422p/. Public domain. |
| `references/root_cellar_interior_habs.jpg` | Cellar interior: masonry vault, hooks, bench | HABS PA-0168, Stamm Farm, Root Cellar. https://www.loc.gov/pictures/item/pa0168.photos.141355p/. Public domain. |

Only buildings, materials and ordinary objects are used as references. No
crime-scene or gore imagery is searched for or used.

## 2. Site plan (metres; +x east, +z north/uphill, y up)

Everything playable sits inside the generic navigation grid (x -8..56.8,
z -24..40.8, 0.6 m cells, 4 layers), so the mission needs no new navigation
code.

```
 z
 40 +------------------------------------------------------------------+
    |  W O O D S   (hillside, rising north; cards + low-poly trunks)    |
 22 |                 ~~~~~ hill ~~~~~                                  |
 18 |           [ROOT CELLAR] 16.5-20.5 x 14.5-18.5, floor -1.4         |
 12 |=====bank=====[door]x18===========bank============================ |
 10 |       lime+shovel, turned ground (26-29, 8-10)        camper      |
  6 | B  [shed]     [kitchen lean-to]   [smoke-               (44,10)   |
  4 | U  6-8.5,     14-19 x 0-3.5       house]     +--BARN--------+     |
  2 | S  2-5        [ MAIN HOUSE  ]     24-27,     |  32-41 x     |     |
  0 |(-1..1.5,      [ 14-22 x -6..0 ]   3-6        |  -8..4       |     |
 -2 | -4..6)  burn  [ 2 storeys    ]#chimney       |  loft z>-1   |     |
 -6 |        barrel [ PORCH -8..-6 ]               +--[big door]--+     |
 -8 |  junk car (8,-8)                                                  |
-12 |       junk car (11,-11)          dog pen (27-30, -12..-9)         |
-14 |    gravel turnaround                                              |
-19 | STAGING (-2,-19)  <== gravel lane from the south-west              |
-24 +------------------------------------------------------------------+
   -8        0         10        20        30        40        50   x
```

The creek runs as a shallow ditch down the west edge (x -7..-5). The woods
close in on all sides. The lane is the only open view, and staging at its end
is out of sight of every window of the house.

## 3. The main house (boxed house, hall-and-parlor, I-house massing)

- **Footprint:** 8.0 x 6.0 m main block, one room deep, two rooms wide.
  - Lean-to kitchen 5.0 x 3.5 m on the back.
  - Full-width porch, 2 m deep, on the front (south).
- **Levels:**
  - Floor on piers at **y 0.30**, reached by two 15 cm steps.
  - Ground-floor ceiling at 2.75, so 2.45 m clear.
  - Upper floor at 2.90. Upper walls to 5.20.
  - Side-gable roof with an east-west ridge at about 7.0.
- **Walls:** single-layer vertical boards, 0.30 m wide and 25 mm thick, nailed
  to a sill and a plate, with 60 mm battens over the joints.
  - Each board is its own breakable world object: wood, health 45, part SKIN.
  - Sills, plates, corner posts and door and window frames are the SUPPORT
    pieces, so a breach cuts boards the way the existing framed-wall layers
    cut skins.
  - Inside, the boards are papered with newspaper, a real Appalachian practice
    and the backdrop for the clippings.
- **Doors:** 0.85 x 2.0 m plank leaves.
  - Two front doors (one per front room, typical of the type).
  - One kitchen back door.
  - One door in each partition.
  - Exterior doors are locked (pick, kick or charge).
- **Windows:** small wooden sash, 0.70 x 1.10 m, sill at 0.85 m above the
  floor.
- **Stairs:** a steep, narrow boxed stair along the back wall of the east room:
  0.85 m wide, 15 risers of 0.173 m on 0.22 m treads, x 18.1 -> 21.4.
- **Chimney:** field stone, 1.2 x 1.4 m, on the outside of the east gable,
  rising past the ridge.

```
GROUND (y 0.30)                         UPPER (y 2.90)
z  3.5 +--------back door------+         
       |   KITCHEN (lean-to)   |               (lean-to roof)
z  0.0 +----door----+----------+----+   +-----------+--------------+
       |            |  ===stair===> |   |           | stairwell    |
       | FRONT ROOM |  STAIR ROOM   |#  | BEDROOM W |    hole       |#
       |  (hall)    | door  (parlor)|#  | (mattress)|  FLYER ROOM  |#
z -6.0 +--door---win+--win----door--+   +--win---win+door--win-----+
       |          P O R C H         |
z -8.0 +----------------------------+
      x14          18              22   x14        18             22
```

## 4. Outbuildings

| Building | Footprint / heights | Construction | Contents |
|---|---|---|---|
| **Barn** | 9 x 12 m, eaves 4.2, ridge 7.2, gable end facing the yard | Vertical grey boards on posts, rusted red standing-seam tin. Big 3 m door opening on the south gable; man door on the west side. | Three stalls along the west wall. Workbench and tools. Hay loft at y 2.6 over the rear 5 m, reached by a steep stair on the east wall. Padlocked chest freezer, a tarp over a shape with one hand showing, a pile of shoes in the last stall, lime bags. |
| **Smokehouse** | 3 x 3 m, walls 2.2, ridge 3.4 | Board walls, low door, no windows | Meat hooks and chains from the joists, a dark-stained dirt floor, a salt box, flies. |
| **Root cellar** | Room 4 x 4 m, floor y -1.4, ceiling 0.8; passage 0.8 m wide with 7 steps down | Stone headwall and plank door in the hill bank at z 12; stone walls; timber ceiling under 1.4 m of earth | Cot with restraints, bare bulb, buckets, jars on a shelf. **The hostage.** |
| **Tool shed** | 2.5 x 3 m | Boards, tin roof | Tools, gas cans |
| **School bus on blocks** | 2.5 x 10 m | Solid cover body | Faded yellow, windows soaped over |
| **Junk cars** | 1.8 x 4.5 m each, two | Solid cover | Rust, flat tyres |
| Small yard pieces | | | Dog pen (empty, chain on a stake, dish), burn barrel, woodpile against the chimney, clothesline, mailbox with a flyer at the lane, creek ditch |

## 5. Mission flow

- **Mission value:** `SWAT_HOLLOW`, appended before `SWAT_MISSION_COUNT`, so
  saved missions and traces keep their numbers.
- **How to start it:**
  - `--mission hollow` on the client and the server.
  - The planning screen's Scenarios tab has a "The Hollow" button.
- **Briefing:** a missing woman's phone was last traced to this property at
  the end of the lane. The owner is armed, and a second man is often seen
  there. Find and recover the hostage, arrest both men, and return to the
  lane.
- **Staging:** at the end of the gravel lane, (-2, 0, -19), with the squad
  facing up the lane. Extraction is beside staging.
- **Occupants:**
  - Actor 1 is the owner, an armed suspect, upstairs in the flyer room.
  - Actor 8 is the accomplice, an armed suspect, at the barn workbench.
  - Actor 2 is the hostage, a civilian, in the root cellar.
- **Completion:** the existing tactical rules. Both suspects arrested (or
  neutralised), the hostage restrained and escorted to staging, evidence
  collected, officers back at staging.
- **Planning:**
  - Drone/cutaway model.
  - Three overwatch posts: the woodline south-east of the house, the hill
    above the cellar, and the lane.
- **Rooms (the `SwatRoom` limit is 8):** front room, stair room, kitchen, west
  bedroom, flyer room, barn, smokehouse, cellar.
  - Each room gets one bare-bulb light (a `SWAT_PART_LIGHT` fixture).
  - The kitchen and cellar bulbs are the ones you see from outside.

## 6. Horror beats, implied, and where they sit

| # | Beat | Where |
|---|---|---|
| 1 | Missing-person flyer stapled under the mailbox | Lane, by staging |
| 2 | Empty dog pen: chain on the stake, dish, no dog | Yard, south-east |
| 3 | Burn barrel with scorched cloth | Yard, west |
| 4 | **Drag marks** in the dirt from the barn and the kitchen door to the cellar door | Yard, north |
| 5 | Lime bags and a shovel by freshly turned ground | Treeline behind the smokehouse |
| 6 | Hooks and chains, a dark dirt floor, flies | Smokehouse |
| 7 | Padlocked chest freezer; a tarp over a shape with one hand showing; a pile of too many shoes | Barn |
| 8 | Dark dried stains on the kitchen planks; one bare bulb | Kitchen |
| 9 | Stained mattress on the floor | Upstairs, west bedroom |
| 10 | Wall of fictional flyers and newspaper clippings, string, a chair facing it | Upstairs, flyer room |
| 11 | Cot with restraints, bare bulb, hostage | Root cellar |

## 7. Asset list and budgets

All geometry comes from re-runnable Blender scripts in
`tools/blender/hollow_*.py`, at real-world scale. All textures are CC0 (Poly
Haven / ambientCG) at 2K for buildings and terrain and 1K for props.
Licences are recorded in `assets/environment/hollow_v1/README.md` and
`PROVENANCE.md`.

| Asset | Budget (tris) |
|---|---|
| Main house shell: boards, battens, porch, tin roofs, sash, stone chimney | <= 40k |
| Barn shell plus stalls, loft and stair | <= 40k |
| Smokehouse, tool shed, cellar headwall and door, cellar interior | <= 10k each |
| Terrain: clearing, lane, ditch, bank and hill | <= 20k |
| Woods: low-poly trunks plus canopy cards, instanced | <= 30k |
| Props: bus, junk car, freezer, cot, mattress, tarp shape, shoe pile, hooks and chains, lime bags, shovel, burn barrel, woodpile, dog pen, clothesline, mailbox, workbench, chair | 500-8k each |
| Decals: drag marks, stains, flyers | Atlas, 1K-2K |

Collision stays authoritative boxes, built in `hollow_impl.h` the way Cedar
House is built. The meshes are a visual shell drawn over them.
