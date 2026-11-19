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
