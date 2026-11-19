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
3. Fault: a forest at 50 m-2 km was lit as evenly as a lawn: every crown fully lit to its foot, no shade
   between crowns (`canopy`, `val3`, `skyh`), and past the shadow cascades (800 m) nothing shades anything.
   Change: the stand shade of pass 1 extends to the lower crown (falling to nothing at the crown's top, by the
   height up the plant, which veg.vs and the impostor card now pass to the lighting), and past the cascades it
   also takes up to 70% of the sun from the lower crowns and trunks. Kept: in `canopy` the crowns separate with
   darker skirts between them, `val3` and `skyh` show shade at the foot of the far stands; a modest change.
4. Fault: the Norfolk araucaria's reduced tiers were broken dashes with sky between (70% of the full crown's
   cover at 30 m, the old tree 70%): its whole fronds lay 1.15 m apart and twice the sprays' width. Change:
   fronds 1 m apart and 2.7 times as wide on the reduced mesh. Kept: 87% and 84% of the full cover, tone
   unchanged (0.99 and 1.01 of the full mesh), tiers read as shelves; reduced triangles 3594 -> 3712 and
   3376 -> 3494.
5. Fault: impostors are fatter, more solid crowns than the meshes they replace (pixels against the full mesh at
   150 m: araucaria 1.41, katsura 1.30, tree fern 1.37; a solid cone where the mesh shows tiers). Change tried:
   the impostor's coverage boost per mip level 0.5 -> 0.3. Reverted: the counts came down a little (1.31, 1.27,
   1.33) and tone held, but in `val3`, `hill` and `skyh` the far stands only got a shade thinner, showing more
   ground; no visible gain.
6. Fault: a forest across a valley (`val3`, `hill`) was a palisade of the same cone in the same tone; the
   reference photo of a Sierra conifer valley is a mottle of lighter and darker crowns. Change: past 30-80 m each
   tree's leaf tone spreads by up to +-32% (from its phase) and its yellow/blue hue shift by 2.6 times, in
   veg.vs and veg_imp.vs alike, so a tree keeps its tone through the swap. A first try at +-16% and 1.8 times
   was too faint to see through the haze. Kept: the far stands read as single crowns of different tones; the
   near trees (under 30 m) are untouched.
