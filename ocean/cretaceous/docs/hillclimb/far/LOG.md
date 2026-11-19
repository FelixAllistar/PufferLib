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
7. Fault: far stands are a saturated teal (`canopy` hue 199, saturation 0.46; `val3`, `hill` and `skyh` 192-199,
   0.29-0.34) where the reference far forest is a dull dark green. Change tried: leaves 60-400 m out 30% less
   saturated and warmer (x1.06/1/0.8). Reverted: the teal did not move (canopy 200/0.45, val3 195/0.28): it is
   the aerial perspective over dark crowns, which the far terms of the plant shader cannot reach; only the
   crowns 150-300 m under `hill` went duller.
8. Fault: the variants pass 2 had not measured (old araucaria and podocarp, the second magnolia and laurel, the
   chloranth, Dicksonia) took their species' tone and missed by 5-12% (chloranth's reduced crown 0.89, old
   araucaria 1.08, second magnolia 1.07). Change: their own entries in `LOD_TONE` and `IMP_TONE`, fitted on a
   20-tree lodmatch run. Kept: every reduced crown within 5% of its full mesh at 30 m (old araucaria about 0.97
   after a last nudge) and every impostor within 5% at 150 m except the Sabal palm (1.11 on 73 pixels).
   Also measured: `capture --path lod_forest` with `tools/lodpop.py` (88 frames with a pop, 21 events); the
   events are the camera passing through a crown at frames 300-350, not level-of-detail switches.
9. Fault: the magnolia's, laurel's and ginkgo's reduced crowns were 12-32% larger than the full crowns at 30 m
   (pixels: magnolia 1.32, second magnolia 1.24, laurel 1.18, ginkgo 1.12): their merged cards overhung the crown,
   so it swelled where the reduced mesh took over. Change: their merged cards 18%, 12% and 10% shorter (a first
   try at 12% on every broadleaf left the plane and the chloranth thinner than their full crowns, 0.86 and
   0.83), leaf tones nudged to hold (`LOD_TONE`). Kept: 1.14, 1.10, 1.10, 1.02; side by side at 30 m the reduced
   crowns now have the full crowns' outline; tone within 5%; triangles unchanged.
10. Fault: past 150 m an impostor was a soft smear (a redwood a blurred green spindle with a diagonal seam of
    light and shade, a katsura a solid mushroom) next to the mesh it replaced, which keeps its lit sprays, dark
    undersides and sky gaps pixel by pixel. Change: the impostor reads its albedo, normal and coverage one mip
    level sharper than its footprint (`IMP_BIAS` -1 in veg_imp.fs and veg_imp_depth.fs), impostor tones
    re-fitted (they came out 1-6% darker). Kept: the redwood's tiers and the katsura's lobes show at 150 m, the
    seam is gone, far stands in `val3` and `skyh` have crowns with shade inside them; pixels against the full
    mesh fell a little (redwood 1.26 -> 1.15 at 150 m); every impostor within 2.5% of the full mesh but the palm
    (1.06).
11. Fault: pass 6's spread lit a third of the crowns 50-200 m out to a lime glare (`mid`): the lightest end
    (+32%) on a light broadleaf crown is brighter than any crown in the reference valley. Change: the spread
    runs from -32% to +16%, more darker crowns than lighter. Kept: `mid`, `val3` and `hill` keep the mottle of
    single crowns without the lime ones and read darker and richer, as the reference does; the levels still
    agree (the same factor on every level of a tree).
12. Fault: the young redwood's reduced crown was 24% fatter than its full crown at 30 m and 21% at 150 m (a
    shorter tree drawn with the mature tree's 1.9 m merged cards): a woolly column where the full mesh is a
    layered spire. Change: 1.6 m merged cards on the young tree, its leaf tone 0.8 -> 0.89 to hold. Kept: 1.13
    and 1.12 of the full cover; the outline is the full crown's at 30 m. (Note: lodmatch moves by up to 4% from
    run to run when the shots before a tree change, through the camera's exposure; tones were fitted on runs of
    the same tree list.)
13. Fault: under a stand at 10-40 m (`river`, `redwood`) some trunks are still pale columns. Change tried: the
    stand shade from 8-30 m instead of 15-40 m. Reverted: the palm trunks and the far redwood trunks darkened a
    little, but the palest ones (the ginkgo's and the plane's, their bark itself near white in the shade) did
    not move; their tone belongs to the near track's bark, and the term reached into its range for no gain.

## Sheets (left `cret/hc`, right this branch; llvmpipe, high preset, 1280x720 halved)

- `01`-`08`: world shots `canopy`, `val3`, `hill`, `mid`, `skyh`, `val7`, `aerial`, `ridge` (cameras in the report).
- `09`: the reduced mesh at 30 m (full mesh, reduced before, reduced after) for the magnolia, the young
  redwood, the araucaria and the tree fern.
- `10`: the impostor at 150 m (full mesh, impostor before, impostor after) for the old redwood, the katsura,
  the araucaria and the ginkgo.

## Final lodmatch (20 trees, luma against the full mesh; before is `cret/hc`)

Reduced mesh at 30 m within 5% of the full mesh: 20 of 20 (before 5 of 20; worst before magnolia 1.33,
ginkgo 0.79). Impostor at 150 m within 5%: 19 of 20 (before 6 of 20; worst before old redwood 1.23,
redwood 1.21, Sabal 1.18; worst after Sabal 1.07 on 55 pixels).
