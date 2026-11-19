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
