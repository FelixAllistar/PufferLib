# alpine: high ground up close (round 2)

One line per pass: the fault, the change, kept or reverted, why. Views (`CRET_SHOT_CAM`, 960x540 while
iterating): `af1` scree 2 m up on the main summit's south face (4416, 3780), `af2` looking up the summit's
west slope (4330, 3616), `af4` across the steep east slope (4500, 3560), `tl1`, `tl2` at the tree line
under the summit, `p3` the third summit (2896, 2560), `c1`-`c5` 2 m from the foot of five of the tallest
faces (`terrashade --faces`), `mtS` and `mtW` the main summit from 420 m and 350 m up. The camera string
is at the end of this file.

1. Fault: cliffs seen from their foot (`c2`) are drawn with a sawtooth of light and dark teeth, one per
   terrain triangle, and with swirled, smeared stripes. Cause: the bedrock skin read its noise along
   `dot(p.xz, tangent)`, a coordinate that turns with the normal: a few degrees of turn moved it by tens
   of metres at the island's scale. Change: `terrain.fs` reads the face in the two fixed vertical planes
   (facing x: along z; facing z: along x) blended by the normal, as triplanar does, and mixes the plan
   and face noise results instead of their coordinates. Kept: the teeth and swirls are gone, the strata
   show as bands on `c2`'s side faces and on `mtS`'s slopes; the cliff bands from afar are unchanged.
2. Fault: scree at 2 m (`af1`, `p3`) is a pale mush of pebbles in soil, not the angular blocks of a talus
   slope. Change: the scree material is Poly Haven `gray_rocks` (CC0, angular grey blocks; was
   `rocks_ground_01`), and `terrain.fs` mixes in scree's second copy at twice the size (`SCREE_COPY`),
   so big and small blocks alternate. Kept: `af1` and `p3` read as talus; unchanged from 400 m (`mtS`).
   Also tried for the rock material: `rock_face_03` and `rock_face_04` (no grain, broken). Not kept:
   orange, and blurrier than Rock030 at 2 m; they did not remove the streaks either (pass 1 did).
3. Fault: cream blotches, 10-30 m long, all over the high slopes (`mtS`, `mtW`). Found by darkening one
   material at a time: scree (tint 0.1) left them, bare soil (tint 0.1) removed them. They are the stony
   soil above the tree line winning small spots in the height blend: its photo is a pale lowland track.
   Change: `terrain.fs` stony soil at 0.6 of its grey (`STONY_SOIL`), and the bedrock skin's value, hue
   and bed terms, whose pale ends multiplied up to 1.8x, eased past 1.1x (`BED_PALE`; on its own that
   removed only a few blotches). Kept: the summit slopes read as one stone mass without the blotches;
   unchanged up close (`tl1`, `af1`).
4. Fault: the rock photo's diagonal grain still reads as one direction of smear at 2-20 m (`af2`, `tl2`). Change: rock's macro copy turned 90 degrees instead of 37. Reverted: marginal; `tl2` turned crosshatched.
5. Fault: within 30 m, rock slopes are flat paint (one photo on a 4 m heightfield) and still read as
   smeared down the fall line at a grazing view (`af4`, `tl2`, `c3`). Change: `terrain.fs` fractured rock
   under 25-60 m: a Voronoi pattern of plates (`FR_CELL` 0.9 m, so 0.4-1.5 m), some split again at
   0.37x, each plate its own tone (0.74-1.22) and its own tilt of the shading normal, dark cracks, read
   triplanar. A first version was a regular polygon net (crazy paving); the cell space is now warped by
   noise so the cracks wander, and some fade out. Kept: `af4` reads as broken stone instead of smears,
   `tl2` and `c3` as rock with joints and slabs. High and medium presets only.
6. Fault: at 30-60 m the plates of pass 5 are one even size and read as cracked mud or reptile scales
   (`tl6`, 30 m over the slope east of the summit). Change: big slabs (3x, 1.5-4 m) carry the pattern to
   60 m; the 0.9 m plates split most of them and fade out from 12 to 30 m, the finer cracks with them.
   Kept: `tl6` shows slabs and joints like bare rock, `af4` and `c3` unchanged in character.
