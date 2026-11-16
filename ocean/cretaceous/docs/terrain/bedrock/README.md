# Bedrock: the pictures

Each picture is the same camera before (left: cret/rock-shoulders) and after (right: cret/bedrock), drawn by the real renderer (`capture`) under Xvfb with Mesa llvmpipe, with the real props and ground textures, at noon (the capture default). The cameras are fixed `CRET_SHOT_CAM` views (the list is at the bottom), so you can render them yourself on a GPU.

What to look for: the rock is the mountain itself. A bedrock shelf is a flank of the mountain worn back into it, a bench floor in front, a face 8-40 m tall behind with a hard bed's top as its rim, and the hillside or a spur either side, so standing on the floor it is a valley. The mountains are bare above a tree line at about 380 m, with cliff bands where hard beds cross their sides.

## Bedrock shelves

Four of the 14 shelves (README, Bedrock), each from 30 m (standing on its floor), 150 m and 500 m:

| Shelf | Where | Face | Pictures |
|---|---|---|---|
| 1 | (3760, 5072), 335 m up | 39 m tall, 234 m across | `face1_30m_pair.jpg`, `face1_150m_pair.jpg`, `face1_500m_pair.jpg` |
| 2 | (3920, 4304), 300 m up | 31 m tall, 292 m across | `face2_30m_pair.jpg`, `face2_150m_pair.jpg`, `face2_500m_pair.jpg` |
| 3 | (5840, 4560), 248 m up | 24 m tall, 145 m across | `face3_30m_pair.jpg`, `face3_150m_pair.jpg`, `face3_500m_pair.jpg` |
| 4 | (5168, 5040), 171 m up, a hill | 14 m tall, 90 m across | `face4_30m_pair.jpg`, `face4_150m_pair.jpg`, `face4_500m_pair.jpg` |

The best ones: `face2_150m_pair.jpg` (two shelves either side of a gully, a treed bench between the faces, the bare mountain above), `face2_500m_pair.jpg` and `face3_500m_pair.jpg` (stacked shelves where there was a forested dome), `face1_150m_pair.jpg` (a face with a bench above it and another band above that), and `face2_30m_pair.jpg` (the face from its floor).

Weak ones, plainly: `face1_500m_pair.jpg` is mostly a near hillside (the camera clears the line to the face, not the frame); `face4_*` is the smallest exposure, on a hill, and in its hazy light reads paler than the mountain faces; up close the face's rim is rounded rather than crisp (the heightfield's 4 m samples), and its blocks are drawn by the shader, so at a grazing angle it is a painted wall rather than real relief.

## Mountains

- `mtn_south_pair.jpg`: from 45 m over the south lowlands at (3400, 1600), toward the highest summit 2.3 km off. Before, a forested dome; after, bare grey mountains standing over the forest.
- `mtn_north_pair.jpg`: from 45 m over the north lowlands at (5500, 6000).
- `mtn_summit_pair.jpg`: from the highest summit (719 m) out over the island to the west.

## What to check on a real GPU

- Whether a face reads as one big rock shelf worked into the mountain from 30, 150 and 500 m, not a pale scar and not a pile of boulders.
- The jointed-block skin on faces over about 55 degrees (beds 1.8-5.5 m, joints 4-12 m): too regular, too strong, too faint?
- The colour of the bare upper mountain: it comes out tan with pale scree patches here; it may want to be greyer.
- Frame cost: under llvmpipe, the terrain pass on the rock-filled `face2_150m` view went from 48 to 136 ms a frame (the whole frame 590 to 908 ms; that view was forest before and is now mostly rock, which is triplanar). On `mtn_south` it is unchanged (32 to 33 ms). The block skin is off on the low preset.

## The cameras

```
CRET_SHOT_CAM="face1_30m:3753:5101:1.8:1.804:0.350:72;face1_150m:3781:4985:73.4:1.804:0.100:55;face1_500m:3861:4645:40.0:1.804:-0.031:40;face2_30m:3943:4264:1.8:-1.049:0.350:72;face2_150m:3883:4368:72.5:-1.049:0.100:55;face2_500m:3708:4673:25.0:-1.049:0.045:40;face3_30m:5849:4552:1.8:-0.771:0.350:72;face3_150m:5762:4636:70.3:-0.771:0.100:55;face3_500m:5510:4881:25.0:-0.771:0.080:40;face4_30m:5170:5039:1.8:2.858:0.350:72;face4_150m:5285:5006:18.3:2.858:0.100:55;face4_500m:5621:4908:60.0:2.858:-0.065:40;mtn_south:3400:1600:45:1.10:0.10:50;mtn_north:5500:6000:45:-2.0:0.08:50;mtn_summit:4420:3612:3:-2.78:-0.22:60" \
  ./build/cretaceous/cretaceous capture DIR --shot 'face*,mtn_*' --size 960 540
```

The positions are for the gcc-built island (seed 1, 2048^2): a clang build bakes a slightly different island, so its shelves sit elsewhere (`bake --verbose` lists them; `terrashade --faces` lists every face 8 m or taller).
