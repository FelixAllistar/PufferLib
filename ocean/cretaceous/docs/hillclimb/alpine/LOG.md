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
7. Fault: a face seen from its foot (`c2`) is a blurred mush. Change: rock's triplanar weights (and the
   fracture layer's) at exponent 12 instead of 4, so a face turned 45 degrees does not show two copies
   of the photo at equal weight. Reverted: no visible change. The blur is the face running away at a
   grazing angle: the ground arrays ask for 8x anisotropic filtering, which a GPU gives and this
   sandbox's software renderer apparently does not.
8. Fault: the talus blocks lie flat on a mid-grey bed (`af1`, `p3`): the photo's gaps are fines, not
   the dark voids between real talus blocks. Change: `terrain.fs` darkens scree by its height map
   (0.4 in the lowest gaps, none on the blocks), near only (gone by 60-150 m, where the mips average the
   height). Kept: the blocks stand up out of dark gaps; `mtS` unchanged.
9. Fault: above the tree line the last conifers are lone stems scattered evenly over bare slopes
   (`tl1`, `tl3`, `mtS`), like lowland trees planted on rock. Change: `terra.c` they stand in tree
   islands: a noise stretched 3:1 along the prevailing wind picks tight clumps (density 0.85 inside,
   none between; more in hollows), and scrub forms a skirt round each clump that reaches 9 m further
   downwind. `TERRA_VERSION` 26. Kept, modest: `tl3` and `mtS` show clumps of trees with open stony
   ground between, as the krummholz reference does. Trees on the island 183,574 to 184,215. The stems
   themselves are still full-height straight trees; their shape is the tree meshes' (another track).
10. Fault: past 25-60 m, where the fracture faded out, steep rock still reads as smears down the fall line
    (the upper slope in `af4`). Change: the fracture (only the big slabs are left there) fades from 40 to
    120 m. Kept: `af4`'s upper slope keeps slabs and joints; `tl1` about the same.
11. Fault: since pass 1 the beds show as many thin contour stripes across the summit's slopes (`mtS`).
    Change: bed layering at 35% on loose steep ground (scree, stony soil), full on rock. Reverted: no
    visible change; the striped slopes are rock (53-73% of the ground over 400 m), where strata belong.
12. Fault: the same fine stripes from afar. Change: the thin layers (2.6 m) fade from 150 to 500 m, the
    thick beds stay. Reverted: a little softer in `mtS`, no change in `mtW`; not clearly better.
