# Biomes hill-climb log (round 2)

One line per pass: the fault, the change, kept or reverted, why. Views (960x540, Mesa llvmpipe; `name:x:z:above:yaw:pitch:fov`):
`desA` 2400:4100:120:-0.785:-0.25 (over the dry plain toward the near-desert), `desB` 3500:4300:260:-2.2:-0.3 (the
near-desert from the air), `desG` 2760:3700:1.8:0.3:-0.02 and `desH` 3050:3650:2:-0.8:-0.03 (on the ground in it), `desE`
2944:3420:3:-1.57:-0.03 (its northern edge), `wetF` 2824:5752:1.8:0.7 (wet valley forest), `rivG` 2296:1600:1.8:3.37
(river mouth), `wetl` 2680:3520:1.8:-2.3 (slow river reach), `beach` 2128:4024:1.8:3.8, `standA` 4168:1576:80:0.7:-0.3
(forest from above), `grs` 2444:3384:30:0:-0.15 (open dry country), plus the brief's `aerial`, `river`, `coast`,
`canopy`, `ground`. A throwaway top-down map of the bake (materials, canopy, the real scatter in a crop) helped place them.

1. Fault: the near-desert's size depends on the island: 27% of the land of a 2 km test island, 9% of the big one,
   because its threshold was fixed while the rain-shadow term is relative. Change: `terra.c` sets the threshold so that
   about 8% of the land (weighed by where a desert may lie at all: lowland, off the rivers) is desert. Now 7.7-8.5% on
   2 and 4 km islands, 7.4% on the big one (was 8.8%). Kept: measured (`CRET_TERRA_DBG=1`), and `desA`/`desB` show the
   same desert, slightly smaller.
2. Fault: the near-desert is one even beige with green grass tussocks over it. Change: `terra.c` breaks it up: pale
   silt pans in flat hollows, dry washes of gravel and some sand on the valley floors and fans (a first try ran them
   down every slope as white streaks: kept off slopes over 0.12-0.25, less sand), stony flats and stony hillsides of
   gravel in 90 m patches, broken rock on the crests, litter under the scrub clumps, a line of thicker scrub and the
   odd small tree along the washes, nothing on the pans; its edge interfingers with the forest over an 80-300 m noise.
   `render/veg.c`: tall grass takes bare earth at 0.25 (was 0.55), so bare earth stops reading as a lawn. Kept: `desH`
   ground is stony grey-brown, `desG` reads as scrub on bare earth not as a meadow, `desB` shows pans and gravel. The
   shader's straw tint still sets most of its tone (not this track's file).
3. Fault: forest is an even lattice of crowns of one age: every conifer drew its age (young, mature, old) by its own
   dice, so a stand never had an age, and stems stood at an even spacing. Change: `render/veg_place.h`: trees stand in
   groups with small openings (a 25 m noise on rotated axes; in an opening about half the lattice sites stay empty),
   and a conifer's age slot is weighed by its stand: old stands (`flora_old`, where the dead wood lies) mostly old
   trees, regrowth patches and the edges of clearings (`flora_gap`) young redwoods, mature trees elsewhere. Kept:
   `canopy` and `standA` show groups, gaps and mixed heights instead of an even pile; `wetF` opened up (taken up in a
   later pass). Trees on the island (the scatter, before the draw caps) 125,085 to 101,539.
4. Fault: riversides look like any other ground: 10 horsetails on the whole island, no bank mud, the bank species
   (giant horsetail, gunnera, royal fern, river plane) almost never chosen. Cause: nearness to water was the 24 m blur
   of the river mask times 3, which is about 0.33 on a bank (rivers are 1-3 cells wide), so every rule asking for
   0.5-0.9 never fired. Change: `terra.c` `terra_riparian` maps the blur so it means what `terra.h` says (1 within
   about 20 m, a half at 26 m, nothing past 40 m), in the bake and in the habitat derived at load. Kept: horsetail
   stands line the water in `wetl` and `river`, river planes and redwoods stand on the bank, the bank canopy opens to
   the water; 16,254 horsetails on the island.
5. Fault: the open country made at load (`world.c` `grassland_build`) is all one straw-coloured grassland, with
   straight lattice runs in its edges, and it runs into the near-desert so the two read as one beige country; there
   is no fern prairie. Change: its noise is rotated and warped (no lattice edges) with a third, finer octave; the
   open ground splits by the habitat's moisture into dry grassland (as before: straw, ferns thinned, grazing) and fern
   prairie (trees gone, ferns kept and thickened, no straw tint): 9.9% of the land is open, 5.0% dry grassland and
   4.9% fern prairie (`CRET_BIOME_DBG=1`). Kept: `airW` (420 m over the west coast) shows forest, green prairie and
   straw grassland as three countries where before there were two; `prA` is a green meadow behind the beach. The
   noise moved the open patches, so `grs` and `desA` now look at forest where they looked at grassland.
6. Fault: the wet valley forest (`wetF`, moisture 0.93) is open woodland on a dry-looking floor, like any other
   forest, and pass 3's openings made it more so. Change: `terra.c` a wet-forest term (the wettest gentle lowland
   below about 260 m, off the dry country): conifers lead (+0.3 of the mix), 25% more stems, moss patches in the
   litter, tree ferns under it in patches; `veg_place.h` keeps it closed (the group openings fade out above
   moisture 0.75). Kept, small: `wetF` closes up, `river` has a tree-fern understorey along the bank; the moss shows
   little under the shader's litter tint. Trees on the island 101,539 to 109,145, tree ferns 25,736 to 28,473.
7. Fault (from pass 4): with nearness to water now 1 for 20 m, the bank sand and bank mud rules spread a 20 m sandy
   and muddy skirt along every river, and the horsetail layer stood at full density over the whole 20 m (a wall of
   horsetails on every flat bank). Change: `terra.c` sand bars and bank mud read the raw blur again (sand only on the
   wide reaches, as before; mud a strip of a few metres); horsetails dense in the nearest few metres and in patchy
   brakes over the rest of a flat bank. Kept: `wetl` has a green riparian corridor (broadleaves, tree ferns, a
   horsetail fringe) through the dry country, `river` keeps its horsetail edge without sand, horsetails on the
   island 25,166 to 6,275. `rivG` stands in a brake at the water's edge.
8. Fault: the beach meets the forest with nothing between, and (from pass 7) where a river crosses the beach its
   horsetail strip stood on the sand. Change: `terra.c` a strand belt behind the beach on low ground near the sea:
   palms in groves, salt-hardy scrub, sandy ground, broadleaves, ferns and tree ferns thinned; horsetails kept off
   the beach. Kept, small: `coast` has scrub on the sand by the river mouth instead of a horsetail wall; the belt is
   narrow on this island, where most low coastal ground is already beach (the bake's berm and `world.c`'s big
   beaches), so `beach` and `cstA` change little. Palms 995 to 1,061, shrubs 88,063 to 93,220.
