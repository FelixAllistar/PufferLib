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
