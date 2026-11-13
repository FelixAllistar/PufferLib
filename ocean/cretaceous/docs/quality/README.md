# Quality presets and level of detail

What each preset draws, how sharp it is on screen, where plants and props
change their level of detail, and what the high preset costs. Measured
natively on Mesa llvmpipe (images and counts; llvmpipe's frame times say
nothing about a GPU), at 1920x1080, from three viewpoints:

- **fern**: a hatchling's eye (0.45 m) in dense ferns at 3292, 5956;
- **forest**: an adult's eye (2.2 m) in tree-fern and broadleaf forest at 1564, 2392;
- **ridge**: the `ridge` capture shot, 25 m over the highest peak.

```bash
CRET_PRESET=high CRET_GL_STATS=1 CRET_CAPTURE_FRAMES=2 \
CRET_SHOT_CAM="vf_fern:3292:5956:0.45:0.0:-0.05:60;vf_forest:1564:2392:2.2:0.0:0.0:60" \
  ./build/cretaceous/cretaceous capture DIR --size 1920 1080 --shot 'vf_fern,vf_forest,ridge'
```

## The presets

| | low | medium | high | ultra |
|---|---|---|---|---|
| render resolution at 1080p | 70% (1344x756) | 80% (1536x864) | 100% | 100% |
| upscale / sharpen | bilinear | Catmull-Rom | sharpen 0.6 | sharpen 0.6 |
| shadow maps | 3 x 1024² to 240 m | 3 x 1024² to 240 m | 4 x 2048² to 800 m | 4 x 3072² to 800 m |
| cascade ends (m) | 22, 80, 240 | 22, 80, 240 | 14, 45, 180, 800 (was 20, 60, 200, 800) | 12, 40, 150, 800 |
| shadow texel, first / second cascade | 5.1 / 18 cm | 5.1 / 18 cm | 1.6 / 5.2 cm (was 2.3 / 6.9) | 0.9 / 3.1 cm |
| smallest near penumbra | 8 cm (1.5 texels) | 8 cm | 5 cm (was 8) | 5 cm |
| full tree mesh to | 19 m | 26 m | 32 m | 43 m |
| tree impostors from | 120 m | 135 m | 150 m | 176 m |
| ground cover (ferns, horsetails; shrubs x1.4) | 54 m | 72 m | 90 m | 122 m |
| ground cover reduced mesh from | 15 m | 20 m | 25 m | 34 m |
| tall grass | 27 m | 36 m | 45 m | 61 m |
| sward tufts | 20 m | 27 m | 34 m | 46 m |
| sward and grass density | 1 | 1 | 1 | 1.4 |
| props (LOD 1 / LOD 2 / cull, in footprint radii) | 11 / 45 / 300 | 14 / 59 / 390 | 22 / 90 / 600 | 31 / 126 / 840 |
| far terrain (CDLOD range) | 1 | 1 | 1 | 1.3 |
| anisotropy: ground and props / plant atlas | 8x / 4x | 8x / 4x | 8x / 8x (atlas was 4x) | 16x / 8x |

Tree switch distances are scaled per tree by its size (below); stones and
debris cull at 200 radii instead of 600, cliffs and coast rocks at 1100.

## Textures and how sharp they are on screen

A screen pixel at 1080 lines and a 60 degree view covers 2.1 mm at 2 m,
10.7 mm at 10 m and 53 mm at 50 m (3.1, 15 and 76 mm on low).

| texture | size sampled | texel | texels per pixel at 2 / 10 / 50 m (high) |
|---|---|---|---|
| ground materials (8, BC3, albedo+height and normal+roughness+AO) | 2048² (the 2K sources; `CRET_MAT_SIZE`) | 1.3-1.6 mm (tiles 2.6-3.2 m), rock 3.4 mm (7 m), sand and moss 2-2.2 mm | 1.4 / 7 / 36: never magnified |
| plant atlas (32 layers, RGBA8, coverage-kept mips) | 512² per layer (`CRET_VEG_ATLAS`) | fronds about 2.5-3 mm, bark 2 mm (a 512 tile about 1 m round the trunk) | 0.8 / 4 / 20; at a hatchling's eye fronds 0.5 m away are magnified 5x |
| props (BC3) | 1024² per material (the 1K Poly Haven glTFs) | about 1-2 mm on a boulder | 1-2 / 5-10 / 25-50 |
| tree impostors | 256² albedo, 128² normal and AO per view (`CRET_VEG_IMP`) | 11 cm on a 30 m conifer | 0.7 at the 150 m switch |

The plant atlas is the one texture that is magnified up close, and the
reason fronds next to the camera look soft: its photo sources are fetched at
256-512 px (`tools/fetch_foliage.sh`) and the atlas is painted at 512. A
1024 atlas would need larger sprites and four times the memory (180 MB
uncompressed); it is left for later.

## Triangles per level of detail

| | full | reduced | impostor |
|---|---|---|---|
| conifers (8 variants, `CRET_VEG_DEBUG=1`) | 9.8-49 k (umbrella araucaria 34-49 k) | 1.9-4.5 k | 2 |
| broadleaves | 3.9-15.8 k | 0.5-2.1 k | 2 |
| cycads, tree ferns, palms | 4.4-9.5 k | 0.6-1.6 k | 2 |
| ground ferns / horsetails / shrubs | 0.4-0.55 k / 1.4-2.2 k / 3.4-7.4 k | 120-144 / 40-56 / 540-794 | - |
| tall grass clump / sward tuft | 64-80 / 16-54 | 16-20 / 8-16 | - |
| props: boulders and mossy rocks / logs, stumps, roots / stones, debris / coast rocks | 4000 / 3000 / 600 / 8000 | 1000 / 750 / 150 / 2000 | LOD 2: 240 / 180 / 40 / 480 |
| dinosaurs (models not in this checkout) | LOD 0 within 9 body lengths | LOD 1 to 30 | LOD 2 |

## Cost on high, before and after (per frame, 1920x1080)

From `CRET_GL_STATS=1`: draw calls, triangles submitted (every pass,
shadows included) and fragment shader invocations (every pass, shadow maps
included).

| | fern before | fern after | forest before | forest after | ridge before | ridge after |
|---|---|---|---|---|---|---|
| draws | 351 | 311 | 424 | 377 | 201 | 188 |
| triangles | 6.28 M | 6.09 M | 7.43 M | 7.22 M | 2.85 M | 2.64 M |
| fragments | 21.2 M | 22.3 M | 23.7 M | 25.3 M | 11.6 M | 11.1 M |

Draws fell by 6-11% and triangles by 3-7%: shadow casters take their mesh
from the cascade, so the second cascade draws the reduced meshes. Fragments
rose by 5-7% in the two close views, nearly all of it in the shadow maps
(the first cascade covers less ground at the same size, so near casters
fill more texels, and ground cover now also casts into the second cascade
out to 44 m); the sharpen reads four more texels in the one full-screen
post pass. Low and medium stay within 4% of before on every count and
look the same (mean pixel difference under 1 of 255).

Ultra, for comparison: fern 319 draws, 7.86 M triangles, 33.3 M fragments;
forest 382, 9.21 M, 39.2 M; ridge 202, 3.56 M, 12.7 M.

## Popping

`capture --path lod_forest,lod_meadow --fps 60` walks (2.5 m/s) then runs
(5.5 m/s) along a straight 40 m line through forest at an adult's eye and
across a grass meadow at a juvenile's, with every frame saved
(`CRET_PATH_STILLS=1`). `tools/lodpop.py` counts tiles whose change between
two frames spikes over both neighbours (see the script), leaving out sky.

640x360, 600 frames each (10 s at 60 fps), high preset, the first 20 frames
left out:

| | forest before | forest after | meadow before | meadow after |
|---|---|---|---|---|
| tiles that popped | 38 | 28 | 74 | 71 |
| frames with a pop | 24 | 26 | 53 | 48 |
| events (8 or more tiles in one frame) | 1 | 0 | 0 | 0 |
| worst frame, tiles | 10 | 2 | 7 | 4 |
| summed excess | 784 | 639 | 2204 | 2206 |

The one event before (forest, frame 383) was a canopy 40-60 m ahead
lighting up in one frame: a tree crossing 32 m switched its shadow caster
to the reduced mesh. It is gone. Most single tiles left in both runs are not
level-of-detail changes: grass blades and a low branch passing close to the
lens, which move several pixels between two frames. Without the sky mask and the
skipped start the meadow counted 390 tiles before, nearly all in the clouds.

What else changed but this path does not show: the sward's tuft switch at
9 m (dithered over 8-10 m now), stones and debris vanishing at their range
(dithered now), ground cover appearing at its full size at the edge (grows
in now) and the impostor fade thinning a crown to about 75% coverage halfway
(complementary now).

### Impostor tone (`tools/lodmatch.py`)

`CRET_GALLERY_MATCH=conifer0,conifer1,broadleaf0,treefern0 capture --gallery`,
before and after. At 150 and 300 m, where impostors take over, every ratio
is within 0.015 of before (for example broadleaf0 impostor/full at 150 m
1.131 before, 1.129 after; conifer0 0.960 and 0.955). At 30 m, near the
full-to-reduced switch, the reduced mesh now matches the full one better
(broadleaf0 1.289 to 1.067, treefern0 1.241 to 1.209, conifer1 1.029 to
0.975, conifer0 1.139 to 1.144): at that distance both now take their self-
shadow from the reduced caster in the second cascade. The same makes the full
crowns at 30 m 2-9% brighter, and the 30 m impostor row (not a distance where
impostors are drawn) moves from 0.89-0.96 to 0.82-0.92 of the full mesh.

## Images

Same viewpoints and time of day, high before (5.0) and after; the crops add
ultra.

- `vf_fern_full.jpg`, `vf_fern_crops.jpg`: a hatchling in ferns. After, the
  fronds shadow each other and the litter with defined edges (1.6 cm texels,
  5 cm penumbra) where before they were soft blots, and the litter's twigs
  read sharper.
- `vf_forest_full.jpg`, `vf_forest_crops.jpg`: bark and frond edges sharper.
- `ridge_full.jpg`, `ridge_crops.jpg`: far forest; little changes on high.

## What still looks low-res or pops

- **Fronds and leaves within a metre or two of the camera** are soft: the
  plant atlas is 512 per layer from 256-512 px photo sprites, magnified up to
  5x at a hatchling's eye. The fix is larger sprites (`fetch_foliage.sh`
  caps them) and a 1024 atlas on ultra, about 180 MB uncompressed.
- **Bark** is the same 512 layer (2 mm texels): fine from 2 m, soft closer.
  The Poly Haven bark sources exist at 2K and more.
- **Far forest from the ridge** is impostors at 256² (11 cm texels on a
  30 m tree) and reads soft and slightly blue past a few hundred metres.
- **No contact shadows under props and plants** beyond the sun maps: the
  terrain has no sky occlusion under rocks, logs and fern clumps (animals
  have it). A cheap screen-space or footprint occlusion is the next step.
- **Shadow casters at the impostor range** still switch from mesh to card
  at 150 m inside the third cascade (45-180 m on high), in one frame; it is
  far and in 21 cm texels, and the paths here did not catch it.
- **The 30 m crowns** are a little brighter than in 5.0 (self-shadow from
  the reduced caster, above).
- **Dinosaur levels** were not seen: the models are not in this checkout.
