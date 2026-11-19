# far: hill-climb log

Track: the trees you see past 30 m (reduced meshes, impostors, their switch distances and the distance terms
of the plant shaders). One line per pass: the fault, the change, kept or reverted, and why.

Views: world shots `canopy` (90 m over a conifer valley), `mid` (20 m up, a mixed stand 50-300 m off), `hill`
(150 m up, a forested coast 0.3-2 km off) and `val3` (35 m up, a forested slope across a valley at 0.3-1 km),
custom cameras in `CRET_SHOT_CAM` (see the report); the gallery's LOD-match pages (`CRET_GALLERY_MATCH`, 13
trees at 30, 150 and 300 m) measured with `tools/lodmatch.py`; the tree gallery pages forced to each LOD.

1. Fault: under the crowns of a forest the bare trunks stood out as pale grey-white poles (worst in `canopy`,
   50-200 m off): a trunk was lit by the whole horizon sky, as if it stood alone in a field. Change: tree bark
   takes the stand's canopy (the terrain's canopy density, as the ground, props and animals beside it do) on its
   sky light, to 0.42/0.5/0.36 of it under a full canopy, faded in from 15 to 40 m (`veg_light.glsl`). Kept: the
   trunks sit in the forest's shade; lone trees in the open are unchanged.
2. Fault: each level of detail drew its crown in another tone, so a forest changed colour where the levels
   took over (lodmatch, luma against the full mesh: magnolia's reduced crown 1.27 at 30 m, laurel 1.22, young
   redwood 1.15, tree fern 1.17, ginkgo 0.79; the redwood impostors 1.21-1.23 at 150 m, katsura 1.13, palm 1.18).
   The reduced mesh's merged cards are whole sprites lighter or darker than the full mesh's clusters, and an
   impostor cannot shade its own crown from the sun as the meshes' shadow maps do. Change: a leaf tone per kind
   and variant for the reduced mesh (`LOD_TONE`, passed in the instance's spare slot) and for the impostor
   (`IMP_TONE`, a uniform per variant), fitted in two rounds of lodmatch. Kept: reduced crowns within 5% of the
   full mesh at 30 m for 16 of 20 trees measured (was 6 of 13), impostors within 5% at 150 m for 18 of 20 (was
   8 of 13); in `mid` and `hill` the reduced broadleaf crowns lost their lime glare and the stand reads as one.
