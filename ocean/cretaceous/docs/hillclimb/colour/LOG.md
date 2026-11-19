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
