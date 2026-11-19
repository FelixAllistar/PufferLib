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
