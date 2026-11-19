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
