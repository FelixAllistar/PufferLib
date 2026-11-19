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
