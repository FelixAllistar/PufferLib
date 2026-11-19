# Exterior props v2: hill-climb log

Branch `swat/art-props-exterior`. Each prop is built by a Blender 4.2 script in
`tools/blender/props_ext_v2/`, rendered with Cycles (CPU, 24 spp + OIDN,
960x540) from a camera matched to its reference photo, compared side by side
(`pe2_compare.py`) and fixed in priority order (silhouette/proportion, then
material/value, then detail). Engine iterations use
`build/swat/props_ext_v2_capture --eye --target --fov` on the real motel
mission. Contact sheets: `<prop>_sheet.jpg` = reference | first Blender render
| final in-game capture.

## Inventory check (origin/5.0, before this pass)

Verified in `assets/environment/`: parked sedan `motel_sedan/BC_SEDAN01`,
picnic table `motel_picnic`, bus shelter `motel_shelter`, dumpster
`motel_dumpster`, pad transformer `motel_transformer`, roadside sign with
VACANCY board `motel_roadside`, wheel stops (motel asset 19), chain-link fence
`motel_fence`, grass tufts and seed heads `motel_road_context`, hose reel and
spigot, power box, fire extinguisher (`motel_props`), ice and vending machines
(`motel_service`), wall sconces, backdrop and road extension
(`motel_backdrop`). Not in the owner's list but present: `motel_backdrop/
near_props.glb` already has lot curbs, a sidewalk, four cobra-head
streetlights, six lot pole lights, near utility poles with wires, a pole
transformer and a hydrant, and the ground decal mask already paints bay
striping. So this pass adds no streetlights, lot lights, hydrant or stall lines;
it goes for vehicles, tall vegetation, street furniture and litter.

## Pickup (BC2_PICKUP01)

Reference: https://commons.wikimedia.org/wiki/File:Chevrolet_c10_pickup_argentina.jpg
(unknown author, public domain; copy in refs/pickup_ref.jpg). Generic late-60s
half-ton, short bed, no badges.

1. pickup iter 1: first block-out (profile-extruded body, cab, bed well, arches, wheels); camera far and too side-on; closer: baseline
2. pickup iter 2: camera to 3/4 front, rims enlarged so body-colour wheels read; closer: yes
3. pickup iter 3: focal 42 mm, cab roof 1.86 m and 0.84 m long, windshield base forward to x=0.80; closer: yes
4. pickup iter 4: side glass to full door-window size with vent pillar, glass darker, wheel r 0.40, arches r 0.50; closer: yes
5. pickup iter 5: paint lighter/bluer, paint grain contrast 0.45 -> 0.22 (no blotches); closer: yes
6. pickup iter 6: ride height (body bottom 0.42 -> 0.50 m), cream painted front bumper, taller; closer: yes
7. pickup iter 7: tyres darker, large white hubcaps; closer: yes (small)
8. pickup iter 8: body bevel 0.045 -> 0.09 for rounder hood/fenders; closer: no (invisible at this size), reverted
9. pickup iter 9: body-colour grille surround, recessed dark grille, inset headlights; closer: yes (small)
10. pickup game 1: in engine at stall (17.5, 15.6); paint far too saturated cyan: COLOR_0 is read as sRGB by the lit shader; closer: n/a
11. pickup game 2: tried linear->sRGB at export with linear-authored colours: blown out white; closer: no, reverted
12. pickup game 3: colours now authored as display tints (Blender linearises for its render, export writes the tint), paint (0.56,0.70,0.76): pale blue like the ref but bumper dark grey and hubcaps black; closer: yes
13. pickup game 4: bumper on the paint material (cream, non-metal), chrome metal 0.9 -> 0.5, paint (0.50,0.65,0.73); closer: yes. Stop (in-game matches hue/value; remaining gaps are body-crease detail).

Budget: 3,520 triangles, 5 materials, one shared 512 px grain JPEG; GLB 217 KB.
