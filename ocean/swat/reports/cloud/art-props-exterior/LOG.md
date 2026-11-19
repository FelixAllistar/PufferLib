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
14. pickup (later): side/rear glass made two-sided (the engine culls back faces; the left windows were inward-facing).

## Panel van (BC2_VAN01)

Reference: https://commons.wikimedia.org/wiki/File:1965_Ford_Econoline_HD_Van_(54486012308).jpg
by Mustang Joe (CC0; copy in refs/van_ref.jpg). Generic mid-60s forward-control
panel van, warm grey, no badges or lettering.

1. van iter 1: block-out (profile body, flat front, wheels, glass, door seams); reads as a box truck: front too flat and square; closer: baseline
2. van iter 2: camera closer/lower, body bevel 0.07 -> 0.13 (rounded roof and corners); closer: yes
3. van iter 3: rounded nose volume (r 0.17 bevel) unioned on the lower front; closer: yes (but buried lamps/grille)
4. van iter 4: lamps, pods, grille and bumper moved onto the nose surface; closer: yes
5. van iter 5: detail offset corrected (were floating 9 cm ahead), camera matched; closer: yes
6. van iter 6: windshield raked (top x 2.02 -> 1.92), belt 1.08 -> 1.02, arches r 0.46; closer: yes
7. van iter 7: warmer paint, grain 0.25 -> 0.16, roughness 0.55 -> 0.40; closer: yes (small)
8. van game 1: west bays nose-out (yaw 180): backlit, unreadable; moved/turned, closer: n/a
9. van game 2: backed into west bay (-20.6, -6.0), front east: lit; paint far too light/cream in engine; closer: yes
10. van game 3: two-sided glass (side windows were culled), paint (0.50,0.48,0.44); closer: yes
11. van game 4: paint (0.42,0.40,0.37), now the ref's warm grey; closer: yes. Stop.

Budget: 3,728 triangles, 5 materials, shared 512 px grain; GLB ~220 KB.

## Desert fan palm (BC2_PALM01)

Reference: https://commons.wikimedia.org/wiki/File:Washingtonia_filifera-_oasis.jpg
by Thayne Tuason (CC BY 4.0; copy in refs/palm_ref.jpg). Tall Washingtonia
with a skirt of dead fronds: the tall element the skyline was missing.

1. palm iter 1: trunk, dead-frond skirt bell, 30 solid fans; camera too far; crown tiny ball; closer: baseline
2. palm iter 2: camera to the ref's worm's-eye (6 m, 20 mm), leaf r 0.85 -> 1.4, petiole 1.5, skirt r 1.15; closer: yes (but a solid green blob)
3. palm iter 3: fans rebuilt as a short web plus separate pointed segments with gaps; closer: yes (now reads as a fan palm)
4. palm iter 4: crown scaled up (leaf r 2.0, petiole 2.0) and raised 0.6 m above the skirt; closer: yes
5. palm iter 5: skirt flared (r 1.35 top), longer (4.6 m), thicker trunk; closer: no (still a clean cup), kept for proportions
6. palm iter 6: 40 leaves, short petioles, elevation span 135 deg (lower leaves droop): crown filled but too narrow; closer: mixed
7. palm iter 7: petiole 1.6 and leaf r 2.3 with 40 leaves: wide, full crown; closer: yes
8. palm iter 8: skirt hem raggeder (1.2 m), deeper ridges, greyer-brown; closer: yes (small)
9. palm game 1: four palms (beside the sign, both street verges, west verge); skirt a pale tan tube and leaves too cyan in engine; closer: n/a
10. palm game 2: skirt (0.40,0.35,0.29), leaves (0.42,0.49,0.36), dead fans darker; closer: yes. Stop.

Budget: 7,722 triangles (two-sided leaves), 3 materials, palm_tree_bark + grain 512 px; GLB ~0.45 MB.

## Station wagon (BC2_WAGON01)

Reference: https://commons.wikimedia.org/wiki/File:1970s_Toyota_Corolla_estate_(wagon)_1.2_Deluxe_(E30)_(10562484414).jpg
by Charlie from United Kingdom (CC BY 2.0; copy in refs/wagon_ref.jpg). Generic
1970s compact wagon, dirty white, black steel wheels, no badges or plates.

1. wagon iter 1: block-out from the pickup recipe (body profile, long-roof cabin, three side windows); camera too far/side-on, glass black; closer: baseline
2. wagon iter 2: camera to the ref's front-quarter, glass lighter; closer: yes (glass still black)
3. wagon iter 3: camera closer (26 mm); closer: yes
4. wagon iter 4: front door window rebuilt to follow the A-pillar, B/C pillars moved to -0.35/-1.15 m (was a degenerate triangle); closer: yes
5. wagon iter 5: two-sided glass twin set back 3 mm (the coincident twin self-shadowed black in Cycles); glass now grey like the ref; closer: yes
6. wagon iter 6: hood front 0.74 -> 0.80 m, belt 0.86 -> 0.90, camera lowered to the ref's 1.3 m; closer: yes (small)
7. wagon iter 7: full-width grille band with the headlamps inside it; closer: yes
8. wagon game 1: south-west bays nose-in: lit side away from camera, windshield blown white (glass 0.45, rough 0.08); closer: n/a
9. wagon game 2: backed in (nose to the street), glass (0.30, rough 0.25): reads; paint blown to pure white; closer: yes
10. wagon game 3: paint (0.70,0.69,0.64) dirty off-white; closer: yes. Stop.

Budget: 3,192 triangles, 5 materials, shared grain; GLB ~0.2 MB.

## Payphone (BC2_PAYPHONE01)

Reference: https://commons.wikimedia.org/wiki/File:Payphone_Main_St_Moab_UT_2025-05-16_11-54-11_1.jpg
by G. Edward Johnson (CC BY 4.0; copy in refs/payphone_ref.jpg). A desert-town
pedestal payphone. All lettering on the real one is replaced by blank colour.

1. payphone iter 1: post, base plate, rounded hood shell (boolean), phone unit, keypad, handset, cord, stickers; camera on the wrong side; closer: baseline
2. payphone iter 2: camera to the ref's front-left, unit widened 0.24 -> 0.30 m, hood deeper (0.44 m); closer: yes
3. payphone iter 3: hood bevel 0.09 -> 0.13, 18 stickers incl. paper on the inner walls, more saturated paint; closer: no (the large bevel broke the hood shell), bevel reverted
4. payphone iter 4: bevel 0.09 with the new stickers and paint; closer: yes
5. payphone iter 5: cream header with a blank white placard inside the top, deeper ledge; closer: yes
6. payphone iter 6: post 0.17 -> 0.22 m wide, hood lowered to 1.0 m; closer: yes (small)
7. payphone game 1: placed on the verge; yellow blown out in engine; closer: n/a
8. payphone game 2: moved to the back edge of the sidewalk (-4.6, 34.85) facing the road, paint (0.76,0.57,0.16); closer: yes. Stop.

Budget: 1,074 triangles, 3 materials, shared grain; GLB ~0.17 MB.

## Newspaper boxes (BC2_NEWSBOX01)

Reference: https://commons.wikimedia.org/wiki/File:Group_of_Newspaper_Vending_Machines.jpg
by Twzink (CC0; copy in refs/newsboxes_ref.jpg). One prop = a row of four
street boxes; titles and logos replaced by blank tone blocks.

1. newsboxes iter 1: blue coin box, tall three-tier red rack, red coin box; camera far; closer: baseline
2. newsboxes iter 2: added the black pedestal box (a key silhouette of the ref), tighter spacing, camera closer; closer: yes
3. newsboxes iter 3: camera higher and closer (22 mm, 1.6 m) like the ref; closer: yes
4. newsboxes iter 4: rusty_painted_metal grain at full contrast, darker bottoms; closer: yes (small)
5. newsboxes iter 5: slanted front/top on the blue box; closer: yes
6. newsboxes iter 6: recessed lower doors on the coin boxes; closer: yes. Stop (Blender).
7. newsboxes game 1: first spot (-8, 34.95) was inside the existing bus shelter; colours far too saturated; closer: n/a
8. newsboxes game 2: moved east of the driveway (6.6, 34.95), paints desaturated, paper 0.88 -> 0.72; closer: yes. Stop.

Budget: 960 triangles, 3 materials, rusty_painted_metal grain 512 px; GLB ~0.1 MB.

## Tyre pile (BC2_TYRES01)

Reference: https://commons.wikimedia.org/wiki/File:Old_tires_stacked_near_a_fence_with_grass_growing_on_top.jpg
by Shixart1985 (CC BY 2.0; copy in refs/tyres_ref.jpg).

1. tyres iter 1: four stacks of lathed tyres, three loose ones, dry grass tufts; tyres read as round donuts, stacks too far apart, 8.6k tris; closer: baseline
2. tyres iter 2: boxy tyre section (flat sidewalls, square shoulders), five stacks packed tight, camera closer and wider, 14 segments (7.7k tris); closer: yes
3. tyres iter 3: rubber darker (0.17 -> 0.10) and tread stripes in vertex colour; closer: no visible change in Cycles (kept: it shows in engine)
4. tyres iter 4: grass denser and finer (34 blades per tuft, 6 mm wide), more drooping; closer: yes
5. tyres iter 5: darker grain map; closer: no, reverted
6. tyres iter 6: stack lean 4 -> 8 deg (stacks lean into each other as in the ref); closer: yes (small). Stop.
7. tyres game 1: first spot (-7.6, -9.6) cut into the laundry annex; closer: n/a
8. tyres game 2: behind the dumpster: reads well (black rubber, straw grass) but within 2 m of the annex door route; closer: yes
9. tyres game 3: moved to (-2.2, -11.4), east of the dumpster and clear of the annex door; final capture. Stop.

Budget: 8,072 triangles, 2 materials, shared grain; GLB ~0.45 MB.

## Discarded mattress (BC2_MATTRESS01)

Reference: https://commons.wikimedia.org/wiki/File:Discarded_mattress_by_trash_bin_(18822330575).jpg
by Eric Fischer (CC BY 2.0; copy in refs/mattress_ref.jpg). The hexagonal
quilting map is drawn by the script (original).

1. mattress iter 1: bevelled slab with procedural hex quilting and white piping, propped on its long edge; camera behind the face; closer: baseline
2. mattress iter 2: camera to the quilted face, ticking darker; closer: yes
3. mattress iter 3: camera mirrored so it leans the ref's way, piping 12 -> 8 mm; closer: yes
4. mattress iter 4: quilt cells 0.16 -> 0.11 m, softer stitches, thicker (0.20 m) with rounder edges; closer: yes
5. mattress iter 5: greyer navy ticking, grime towards the ground edge; closer: yes (small)
6. mattress iter 6: more face sag; closer: no visible change, not kept. Stop.
7. mattress game 1: leaning on the dumpster's east end: top edge cut into the lid; closer: n/a
8. mattress game 2: moved 0.45 m east so it rests against the dumpster; closer: yes. Stop.

Budget: 1,328 triangles, 2 materials, 256 px quilt PNG + 256 px grain; GLB ~0.1 MB.

## Tumbleweed (BC2_TUMBLE01)

Reference: https://commons.wikimedia.org/wiki/File:Tumbleweed_038.jpg
by ImperfectTommy / Edmond Meinfelder (CC BY 2.0; copy in refs/tumbleweed_ref.jpg).

1. tumbleweed iter 1: 40 main twigs arcing from the base over a squashed sphere, 3 forks each, 3-sided tubes; too sparse and small; closer: baseline
2. tumbleweed iter 2: radius 0.45 -> 0.5 m, 55 mains x 4 forks, camera closer; closer: yes
3. tumbleweed iter 3: twigs 8 -> 5.5 mm, paler straw; closer: yes
4. tumbleweed iter 4: twig wobble 0.08 -> 0.16 (crooked twigs); closer: yes (small)
5. tumbleweed iter 5: mains also cover the top (was an open bowl); closer: yes
6. tumbleweed iter 6: 72 mains x 3 forks (denser shell, same budget); closer: yes (small). Stop.
7. tumbleweed game 1: six placed against curbs and lot edges, random yaw/scale 0.75-1.05; reads, a little too pale; closer: n/a
8. tumbleweed game 2: straw (0.76,0.68,0.52); closer: yes. Stop.

Budget: 7,776 triangles, 1 material, 256 px grain; GLB ~0.4 MB. Drawn 6x (~47k tris total, frustum culled).
