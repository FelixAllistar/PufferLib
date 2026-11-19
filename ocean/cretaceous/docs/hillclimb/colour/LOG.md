# colour: hill-climb log

Track: the greens of the forest (round 3): the plant shaders' colour terms (`shaders/veg_light.glsl`, the
colour parts of `veg.fs` and the impostor shaders) and the tone and tint tables in `render/veg.c`. One line
per pass: the fault, the change, kept or reverted, and why.

Views (960x540 while climbing, llvmpipe, high preset): world shots `canopy`, `aerial`, `ground`, and the far
track's cameras `val3`, `mid`, `hill`, `skyh`, plus `ga0` (fern prairie at a hatchling's eye). Numbers are
from a small script over the vegetation-like pixels of each shot (median hue and saturation; "lime" is the
share of them at hue 65-105, saturation over 0.5 and value over 0.35; "teal" the share at hue over 160).
Reference photos (Wikimedia Commons, looked at only, not kept): Araucaria araucana forest, Metasequoia in
summer, Laurus nobilis, Cycas revoluta; their foliage measures hue 70-100.

Baseline (`cret/hc`): canopy hue 180 / sat 0.35 / teal 81%; hill 170 / 0.36 / 56%; skyh 189 / 0.23 / 91%;
aerial 188 / 0.23 / 98%; ground 109 / 0.49 / lime 25%; ga0 106 / 0.49 / lime 24%; mid 98 / 0.44 / lime 28%.
With the haze taken off the plants only, canopy measures hue 142 / sat 0.55: the crowns are a rich dark
green under it, and the teal is mostly the haze.

1. Fault: conifer slopes teal (canopy hue 180). Change tried: tree crowns filter the sky fill harder (less
   blue) than the ground cover. Reverted: canopy 180 -> 178, hill unchanged; nothing to see. The sky fill is
   not where the teal comes from.
2. Fault: the same. The haze (aerial perspective) lays the full horizon-sky blue over dark crowns, and a
   dark green plus a saturated blue is teal. Change: on foliage and bark the in-scattered part of the haze
   is taken half way to its own grey (brightness kept), in `veg_shade`. Kept: in `canopy` the conifers
   100-400 m off lose their blue glaze and read dark green; the far stands of `skyh` go grey-green; canopy
   teal share 81% -> 74%, skyh 91% -> 85%.
3. Fault: the conifers' own leaves are blue-green (podocarp table hue 124, araucaria ropes painted at
   126) and all conifers are one green; the brief wants a blue-black araucaria. Change: podocarp leaf mean
   0.024/0.056/0.026 -> 0.03/0.06/0.02 (hue 100); the araucaria and cheirolep ropes painted darker, a
   little blue at the base and less yellow at the tips (base 0.026/0.066/0.028 -> 0.022/0.05/0.026, tip
   0.06/0.115/0.03 -> 0.045/0.085/0.03). Kept: at 50-150 m in `canopy` the araucarias now read as dark,
   blue-black tiers beside the greener podocarps; the shot's numbers hardly move (hue 175 -> 177).
4. Fault: still teal (canopy hue 177, hill 167, skyh 188). Change: the foliage haze keeps a fifth of the
   sky's colour instead of half. Kept: in `canopy` the crowns 100-400 m off read green, not blue-filmed;
   on `hill` the far forest goes grey-green like the reference valley while the terrain haze behind it is
   unchanged; canopy 168 / teal 61%, hill 158 / 48%. What is left of the teal in `aerial` and `skyh`
   (187-189) is mostly 2-8 km out, where the terrain shader paints the far canopy (not this track's file).
5. Fault: neon lime ground ferns and fern prairie (`ground`, `ga0`: a quarter of the green pixels lime,
   saturation 0.49). Change: the ground cover's saturation offset in `veg_shade` -0.22 -> -0.38. Kept: the
   fronds read as a natural mid green in sun and shade, the prairie at `ga0` no longer glows; lime share
   24-25% -> 15-17%, saturation 0.49 -> 0.44-0.46; trees untouched.
6. Fault: in `mid` a third of the broadleaf crowns in sun are a candy or lime green, and the whole stand
   more saturated than the reference forest photos. Change: the leaves' saturation boost under a high sun
   0.05-0.2 -> 0-0.1 (the ground cover's offset -0.38 -> -0.33, so it ends a little further down than in
   pass 5). Kept: the stand reads as photographed rather than painted, the lime crowns calm down; lime
   share mid 28% -> 20%, ga0 15% -> 10%; conifer slopes unchanged.
7. Fault: species differ too little in green: in the gallery over sand every tree is one dark olive, and
   the reduced meshes share leaf layers across species (the ginkgo's crown is the laurel's layer, the
   plane's the katsura's), so a species' green cannot live in its atlas layer. Change: a leaf tint per
   species (`LEAF_TINT` in veg.c, by kind and variant), set per draw on the meshes (`u_leaf_tint`, veg.fs)
   and per variant on the impostors (`u_imp_tint`, veg_imp.vs/.fs), so all three levels take the same tint:
   blue-black araucaria and cheirolep cypress, warm podocarp, fresh dawn redwood, darker glossy laurel and
   magnolia, cool katsura, yellow-green ginkgo and plane, olive cycads. A first try (ginkgo x1.45/1.2/0.66,
   plane x1.3/1.12/0.74) lit the ginkgos at 100 m in `mid` a neon yellow (the far hue spread amplifies
   them); toned down to x1.24/1.1/0.74 and x1.2/1.08/0.78. Kept: the gallery and `mid` show the species
   apart (yellow-green ginkgo and plane among darker laurels and blue-black araucarias), no neon.
   One multiply per leaf pixel; one more uniform per mesh draw.
8. Fault: the lime crowns left in `mid` (pale mint-lime at 80-150 m). A debug colouring (chloranth magenta,
   katsura blue, then ginkgo red, plane blue) showed every one of them is a ginkgo, lime before pass 7 too:
   its leaf layer is the brightest, most saturated broadleaf green, and its reduced crown is the laurel's
   layer lit 1.52 times. Change: the ginkgo's tint darkens it (x1/0.85/0.9) instead of yellowing it; the
   katsura's a little darker and cooler (x0.86/0.9/0.98). Kept: the ginkgos read yellow-olive inside the
   stand instead of mint-lime; the gallery's katsura and laurel a shade darker; lime share in `mid` hardly
   moves (19%: it counts the sunlit grass too).
9. Fault: after pass 4, the stands 2-4 km off in `skyh` are near-black grey blotches on hills that haze blue
   (the terrain keeps the haze's full colour). Change: the foliage haze keeps a fifth of the sky's colour to
   600 m, rising to 70% by 3 km. Kept: the far stands sit in the blue of their hills again while the teal
   at 100-600 m (`canopy`, `hill`) stays gone; canopy 172, hill 160 unchanged.
   Check (after pass 9): `lodmatch.py` on araucaria, podocarp, dawn redwood, cypress, laurel, katsura,
   ginkgo and plane: reduced crowns at 30 m 0.955-1.037 of the full mesh, impostors at 150 m 0.967-1.042,
   hues alike; the tints hold through the levels.
10. Fault: the umbrella crowns on the fern-prairie slopes of `ga0` (20-40 m) are a lime grass-green with
   yellow patches, and the cycads (brief: olive) are the same saturated green as every fern. A debug
   colouring showed the umbrella crowns are stout tree ferns, not cycads. Change: the cycad leaf layers
   (`L_CYCAD`, `L_LEAFLET`) olive, 0.026/0.066/0.028 -> 0.042/0.058/0.03, with small shifts by species
   (Zamia darker, Williamsonia warmer) in place of pass 7's yellowing tint; the tree ferns a tint that
   deepens them (x0.96/0.92/0.96, Dicksonia x1.04/0.86/0.92). Kept, a modest step: in the gallery the
   cycads read olive beside the green tree ferns; in `ga0` the tree-fern crowns go deeper, less lime.
11. Fault: in `mid` the meadow sward 0-40 m out is greener than the terrain's olive grass beyond it, so its
   end draws a line. Change tried: an olive tint (x1.1/1/0.82) on the sward and tall-grass kinds in
   `LEAF_TINT`. Reverted: nothing moved, so the green there is not the plant meshes' (most likely the
   terrain's own ground material, `terrain.fs`, outside this track).
12. Fault: in `redwood` (3 m, under a stand) and `ground` the ferns in shade are an emerald green that
   glows against the dull floor: their light there is mostly sky fill, which goes through the leaves'
   green filter at full weight. Change: ground cover takes the filter at 0.4 instead of 0.75 (trees
   unchanged). Kept: the shaded ferns sit in the floor's shade as a muted green, sunlit fronds unchanged;
   ground lime share 10.6% -> 5.5%, ga0 10% -> 8%.

## Sheets (left `cret/hc`, right this branch; llvmpipe, high preset, 960x540)

- `01`-`07`: world shots `canopy`, `hill`, `skyh`, `val3`, `mid`, `ground`, `ga0`.
- `08`-`12`: the tree gallery, pages 1, 3, 4, 5 and 7 (conifers; magnolia, cypress, dawn redwood; katsura,
  laurel, ginkgo; plane, laurel, magnolia; cycads).

Cameras (`CRET_SHOT_CAM`): `val3:2104:1670:35:6.06:0.06:55`, `mid:2322:1684:20:1.24:0.18:60`,
`hill:2278:3958:150:1.13:-0.12:55`, `skyh:2896:2432:4:0.6:0.12:70`, `ga0:2620:1344:2.5:0:0.08:70`,
`redwood:4610:1765:3.0:0.72:-0.03:65`.

## Final numbers (before -> after)

canopy hue 180 -> 172, teal share 81% -> 66%; hill 170 -> 160, 56% -> 50%; skyh 189 -> 188, 91% -> 84%;
aerial unchanged (188, 98%: from 6 km up the island is the terrain's painted canopy); lime share ground
25% -> 5%, ga0 24% -> 8%, mid 28% -> 16%; val3 hue 106 -> 103.
