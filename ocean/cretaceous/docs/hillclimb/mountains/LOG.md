# Mountains hill-climb log

One line per pass: the fault, the change, kept or reverted, why. Views are `mtD` (the main summit from
the south, 450 m up), `mtE` (from the south-west, 300 m up), `mtA`, `skyh`, `ridge`, `aerial` and the
brief's world shots; renders at 960x540 on Mesa llvmpipe.

1. Fault: green fern turf reaches up under the 719 m summit in tongues and patches. Change: `terra.c`
   `surface`: a `summit` ramp (tree line +40 m to +230 m, raised on exposed crests, lowered in hollows)
   takes 92% of the fern ground, 85% of the fern layer and most of the alpine moss, and gives it to
   bare stony soil; `terrain.fs`: living cover above 400-560 m is tinted brown-green. Kept: no green
   left within ~150 m of any summit, the turf below reads brown-olive, nothing lower changed.
2. Fault: cream blotches on the high slopes. Change: fade the dry-grassland straw tint above 330-450 m. Reverted: the cream stayed (it is not the straw tint) and fern green came back on the high benches it had been hiding.
3. Fault: scree reads as pale cream bands and washed-out slopes (its photo averages 135 against the
   rock's 79). Change: `terrain.fs` scree tint 0.7 grey, slightly cool. Kept (judged with pass 4 against
   a true baseline at `alp2`, 20 m over the summit's south face): the scree slope is grey stone, not cream.
4. Fault: the alpine turf tint from pass 1 leaned straw-yellow. Change: grey-brown instead. Kept, small:
   the turf patches are browner; the yellow streaks up close turned out to be bare soil (pass 5).
5. Fault: yellow-ochre streaks of bare soil down the high slopes (the orange lowland soil photo). Change:
   `terrain.fs` soil greys toward a stony grey-brown from 360-500 m; `terra.c` the summit ramp gives
   less soil (0.2) and some scree instead. Kept: `alp2` streaks are grey-tan stony ground, `mtD`'s high
   blotches calmer.
6. Fault: the lower slopes are open parkland, not forest. Cause: the scatter keeps a tree with a chance
   of its layer's density squared, so a stand split half conifer, half broadleaf had half the stems of a
   pure one. Change: `terra.c` raises both canopy layers by 1/sqrt(c^2 + (1-c)^2), so a stand's stems
   do not depend on its mix. Kept: the valley forests in `mtE` and `mtA` close up. Cost: 164,025 trees
   on the island to 214,970 (+31% instances; meshes unchanged).
7. Fault: green turf and forest run right up to the foot of every cliff band; no scree fans. Change:
   `terra.c` a talus field: from each sample, walk up the fall line; rock steeper than 45 degrees within
   12-35 m (a noise along the foot, so the fans come and go) and at least 8 m higher lays scree there,
   fading with distance, mostly above 150-380 m; trees thin by half on it, ferns by 70%. A first try
   (any rock over 40 degrees, 20-55 m, 4 m drop) put scree on 24% of the land and stripped the forest:
   tightened before keeping. Scree tint 0.6 (from 0.7). Kept: grey aprons under the bands in `mtD` and
   `mtE`, the forest kept (213,715 trees).
8. Fault: cream patches on the high benches. Change: stony soil 22% darker. Reverted: no visible change, the cream is not the soil's colour.
9. Fault: the same cream patches. Change: the hard beds' pale layering only at 35% on scree and soil. Reverted: no visible change.
10. Fault: cream patches still all over the high slopes. Found with a throwaway probe (dominant material
    per pixel, and the albedo before lighting): every one is scree, whose albedo after the ground
    material build is about 30% above the rock's even at tint 0.48; in the frame it was 171 against the
    rock's 133. Change: scree tint 0.4 (cool grey). Kept: 153 against 135 in the frame, the high slopes
    read as one grey stone mass with lighter talus (`mtD`, `alp2`, `skyh`).
11. Fault: no dry country; the island is fern and forest from the beach to the tree line. Change: `terra.c`
    near-desert scrub: a 1.5 km noise plus the mountains' rain shadow picks a few lowland regions (under
    140-260 m, off the rivers, banks and wet hollows; 8.8% of the land, its heart near 2944, 3584): bare
    earth and sand, ferns -85%, trees -93%, cycads and scrub in clumps with bare ground between, moisture
    -75% so the rest of the bake follows. New views `des1` (2 m up) and `des2` (120 m up). Kept: a place
    that reads as a different country from the air and on the ground (trees 213,715 to 187,775).
12. Fault: the desert read as pale beach dunes (too much sand, whose tint is 2.2x) and even straw. Change:
    sand only in pans on the flats; `terrain.fs` bare earth in dry country is red-brown, not straw. Kept.
13. Fault: from afar the near-desert is an even beige. Change: `terrain.fs` dark olive-grey dots of dwarf
    scrub at about 3 m, gathered in loose 30 m stands, on dry bare earth past 25 m. A first version read
    the plain noise and laid the dots on a visible grid; the quasi-periodic read fixed it. Kept, small:
    `des2` slopes are speckled like dry scrubland. Cracks in the flat earth were tried in the same pass
    and dropped: they did not show in `des4`.
