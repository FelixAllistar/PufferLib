# Plant cards: volume and detail

You said the plants look flat and low resolution even on the highest preset,
and that leaf cards seen edge on show as infinitely thin planes. This page is
what was measured, what changed, how to switch each change off to compare,
and what it costs. Branch `cret/plant-cards`.

## 1. Audit (before)

`./build/cretaceous/test/test_veg --audit` (tests/test_veg.c) builds every
plant mesh and measures its leaf cards. Screen: 1080 rows over the game's 60
degree vertical field of view, so a pixel is 1.07 mm wide at 1 m. Plants at
the middle of their scale range. "Edge on" is the leaf area whose own card
normal is within 11.5 degrees of square to the view (|n.v| under 0.2: the
card shows at under a fifth of its width), seen from an adult's eye (1.8 m)
on a ring round the plant.

| family | leaf triangles (full mesh) | texel (mm) | texels per pixel at 2 / 5 / 15 m | edge on at 2 / 5 / 15 m | under a tenth of its width at 5 m | shading normal off the card (median / 90th pct, degrees) |
|---|---|---|---|---|---|---|
| tall grass | 70 | 1.51 | 1.41 / 3.54 / 10.6 | 19% / 18% / 18% | 9% | 36 / 45 |
| sward tufts | 56 | 0.57 | 3.75 / 9.38 / 28.1 | 17% / 28% / 32% | 14% | 23 / 52 |
| ground ferns | 550 | 1.38 | 1.55 / 3.87 / 11.6 | 18% / 39% / 47% | 20% | 12 / 41 |
| cycads | 4249 | 0.47 | 4.58 / 11.46 / 34.4 | 29% / 32% / 30% | 16% | 24 / 39 |
| shrubs | 1084 | 1.63 | 1.31 / 3.27 / 9.8 | 21% / 23% / 24% | 11% | 38 / 53 |
| broadleaf crowns | 3312 | 2.09 | 1.02 / 2.56 / 7.7 | 13% / 17% / 21% | 9% | 35 / 53 |
| conifer crowns | 6998 | 1.77 | 1.21 / 3.02 / 9.1 | 13% / 14% / 21% | 7% | 36 / 54 |
| palms | 5572 | 2.68 | 0.80 / 1.99 / 6.0 | 9% / 15% / 28% | 7% | 24 / 38 |
| tree ferns | 3302 | 2.20 | 0.97 / 2.43 / 7.3 | 15% / 23% / 27% | 11% | 25 / 42 |
| horsetails | 1458 | 0.71 | 3.00 / 7.49 / 22.5 | 24% / 45% / 48% | 23% | 18 / 28 |
| flowers (petals) | 44 | 0.56 | 3.81 / 9.53 / 28.6 | 18% / 23% / 22% | 12% | 5 / 7 |

What that says:

- **Texture resolution is not the limit past about 2 m.** The atlas layer is
  512 texels (`AT`, `CRET_VEG_ATLAS` only lowers it). At 2 m it gives about
  one texel per pixel on crowns, palms and tree ferns (0.8 to 1.2), more on
  grass, ferns and cycads. From 5 m on every family is minified 2 to 34
  times, so what you see is the mip chain, not the top level. A 1024 atlas
  would only sharpen plants within about 2 m (and a hatchling's 0.5 m view),
  for four times the memory: 224 MB instead of 56 MB (42 layers, RGBA8, with
  mips). Not done; see open questions.
- **The photos are smaller than the atlas cells anyway.** `fetch_foliage.sh`
  cuts ambientCG's 1K leaf sets and Poly Haven's 2K fern into sprites capped
  at 256 or 512 px: broadleaf, podocarp and shrub leaves are 63-197 x 256 px,
  grass blades 26-97 x 512, fern fronds 128-288 x 512, gunnera and plane
  leaves about 300 x 300 to 424 x 438. A crown cluster stamps several leaves
  into a 256 px cell, so each leaf lands at 60-100 px: the atlas is the
  limit there, not the photo.
- **Mips.** The atlas is uncompressed RGBA8 sRGB (no BC3), with alpha-coverage
  mips (each level's alpha is rescaled so the alpha test keeps the coverage
  of the top level: crowns stay full in the distance). Colour mips were a
  2x2 box average; a sharper filter made no visible difference (section 2).
  8x anisotropy on high and ultra.
- **Lighting.** Normals were already bent: each vertex's normal leans toward
  the open sky round the crown (`bake_sky`, 12 to 38 degrees off the card),
  lit by a squared half-Lambert, so crowns already shade as one volume. But
  within a card the normal barely changes: a leaf, frond or blade is one
  flat value across its width, which is what reads as a flat plane up
  close.
- **Edge on.** A sixth to a quarter of the leaf area of trees is within 11.5
  degrees of edge on from an adult's eye, and close to half of ground ferns
  and horsetails at 5 to 15 m. Under 0.1 (a card showing a tenth of its
  width or less, drawn as a line): 7 to 23%. Nothing faded them.

## 2. What changed

Three changes. Each is on by default on every preset and has a switch, read
at start, for an A/B comparison. None is tied to a preset because none costs
enough to need it (section 3).

| change | switch | where |
|---|---|---|
| Edge-on fade, and backlight by the card's own normal | `CRET_VEG_EDGE=0` | `veg.fs`, `veg_depth.fs`, `common.glsl`, `veg_light.glsl` |
| Rounded cards | `CRET_VEG_ROUND=0` | `veg_gen.h` (baked into the mesh) |
| Centre crease on blades and leaflets | `CRET_VEG_CREASE=0` | `veg_gen.h` (baked into the mesh) |

**Edge-on fade.** Each leaf card's own flat normal comes from the screen
derivatives of its camera-relative position (a new `v_rel` varying: world
positions thousands of metres out lose the millimetres a derivative needs).
As the card turns edge on, its alpha is multiplied down before the alpha
test, so its thinner parts go first and then the rest, over a short band of
angles. Each card leaves at its own angle (a cut between |n.v| 0.03 and 0.15,
from the per-leaf variation key), so a crown does not lose all its edge-on
cards at once. Every card is whole above |n.v| 0.2 (78 degrees off face on).
The depth prepass cuts exactly the same. Shadows are not faded: a card edge
on to you is not edge on to the sun, so it still casts its shadow.
The same flat normal now decides the backlit glow: a leaf you see from the
face away from the sun glows yellow-green, whichever way its bent normal
leans (it was judged by the bent normal, so whole sides of a crown glowed or
did not).

**Rounded cards.** Every leaf card, frond and blade leans the normals at its
two edges 26 degrees outward (`VEG_ROUND_TILT`), as across the top of a
stem, so light falls off across its width. Cards are first turned to face
up and away from the plant's axis, so the rounding bulges outward. Then the
crown bend applies as before. The old keeled fronds (shield fern, palms,
ribbons), whose edges fold up into a V, lean their edge normals inward with
their fold instead. Costs nothing per pixel.

**Centre crease.** Frond leaflets at least 5 cm wide (tree-fern pinnae,
palm leaflets, fern pinnae; not the cycads' 2 cm ones) and the flat blades
of tall grass and the sward get a centre vertex on their axis, and their
edges fold back by a quarter of the half width (14 degrees). Seen face on
the outline is exactly the same (`test_veg` checks the area); edge on they
show a thin shaded wedge instead of a line, and the edge-on fade leaves them
alone (each half is 14 degrees off). Full meshes only, so only near plants
pay for it: the reduced meshes, from 25 m for ground cover, 15 m for tall
grass and about 26 m for tree ferns and 29 m for palms (on high, scaled by
the plant's size), stay flat.

**Tried and dropped: sharper atlas mips.** Each colour mip as a Lanczos-2
downsample (8x8 taps, alpha-weighted, clamped to the 2x2 texels it replaces
so it cannot ring) instead of the 2x2 box. Free at run time, but on the four
close review shots it moved the mean pixel by under 1/255 and changed 0 to
0.4% of pixels by more than 6/255: not visible, so not kept. Unclamped it
would sharpen more, at the price of halos round every leaf.

**Tried and dropped: a leaf relief map.** A normal map per atlas layer from
blurred alpha (each leaf a low dome) and luminance (veins and gaps lower),
two-channel BC3, 14 MB with mips, tilting leaf normals through a tangent
frame from screen derivatives. It changed 1 to 3% of the pixels of the close
review shots even at twice the slope: ground cover mixes 60% of its normal
toward straight up (so a fern seen from below does not draw black), and
per-leaf slopes are finer than the mips keep past a metre or two. Not worth
the memory and a texture fetch in the costliest plant shader.

## 3. Cost

There is no GPU in the sandbox, so the frame was measured on Mesa's software
rasterizer (llvmpipe under Xvfb) with the game's own counters:
`CRET_GL_STATS=1` (draw calls and triangles per pass, fragments from a
pipeline statistics query) and `CRET_GPU_PROFILE=1` (per-pass timer
queries), over 8 frames after the usual 64 warm-up frames, high preset,
1920x1080. "5.0" is origin/5.0 built and run from its own worktree (its
own shaders); "off" and "on" are this branch with all switches off and on.
The counts are exact; llvmpipe's milliseconds are a CPU rasterizer's, good
for comparing runs but noisy by a few percent (5.0 and "off" should match
and differ by up to 3.5%).

| shot | triangles 5.0 / on | fragments 5.0 / on | plant passes, llvmpipe ms 5.0 / off / on | whole frame, llvmpipe ms 5.0 / off / on |
|---|---|---|---|---|
| `ground` (meadow, 1.7 m) | 5.07 / 5.08 M (+0.2%) | 24.74 / 24.73 M | 885 / 900 / 908 (+2.6%) | 1532 / 1542 / 1554 (+1.5%) |
| `dino_tp` (ferns and tree ferns) | 4.48 / 4.53 M (+1.1%) | 20.49 / 20.46 M | 1083 / 1116 / 1176 (+8.5%) | 1519 / 1558 / 1622 (+6.7%) |
| `redwood` (grove, 3 m) | 10.26 / 10.27 M (+0.1%) | 32.98 / 32.96 M | 2245 / 2239 / 2297 (+2.3%) | 2978 / 2962 / 3033 (+1.9%) |
| `vf_fern` (hatchling in ferns) | 3.42 / 3.48 M (+1.8%) | 26.09 / 26.06 M | 772 / 813 / 833 (+7.9%) | 1469 / 1521 / 1540 (+4.8%) |
| `vf_forest` (tree-fern forest) | 5.72 / 6.16 M (+7.7%) | 25.65 / 25.67 M | 1891 / 1924 / 2034 (+7.6%) | 2559 / 2599 / 2679 (+4.7%) |

So the whole frame stays within the 10% budget on every shot (+1.5 to
+6.7%). Where it goes:

- **Triangles**: the crease, and only on full meshes, so it is near plants
  that pay. Over all meshes it adds 5.6% vertices and 8.9% triangles
  (`test_veg`); per kind: tree ferns +65% triangles, grass +100% (70 to 140
  per clump), sward +38%, ground ferns +26%, palms +20%, cycads +5%, the
  trees none. On screen that is +0.1 to +1.8%, and +7.7% in the tree-fern
  forest. Shadow casters carry the crease too (in the counts above).
- **Fragments**: unchanged (the crease keeps every card's outline; the fade
  removes a few edge-on slivers).
- **Per pixel**: the edge fade and the card-normal backlight add two screen
  derivatives, a cross product and a few multiplies to the plant colour
  shader, and the derivatives and fade to the plant depth prepass; one more
  vec3 varying. Rounding costs nothing per pixel (it is in the mesh).
- **Memory**: about 1 MB more vertex data and 0.5 MB of indices for the
  meshes; no new textures. **Startup**: mesh generation does 9% more
  triangles (all meshes take about 0.4 s on one core in `test_veg`); the
  atlas is unchanged, so its cache is still valid.

Not measured here: a real GPU's numbers. To check on yours, run the same
with `CRET_GPU_PROFILE=1 CRET_CAPTURE_FRAMES=8` once as is and once with
`CRET_VEG_ROUND=0 CRET_VEG_CREASE=0 CRET_VEG_EDGE=0` (section 4 has the
command).

## 4. Pictures

Rendered with the real `capture` tool on llvmpipe under Xvfb (no GPU in
the sandbox), high preset, 1280x720; "before" is every switch off, "after"
every switch on. The fixed cameras:

```bash
CRET_SHOT_CAM="redwood:4610:1765:3.0:0.72:-0.03:65;gup:2620:1344:45:0.5:-0.7:70;vf_fern:3292:5956:0.45:0.0:-0.05:60;vf_forest:1564:2392:2.2:0.0:0.0:60" \
  ./build/cretaceous/cretaceous capture DIR --size 1280 720 --shot ground,redwood,canopy,gup,dino_tp,vf_fern,vf_forest
# before: the same with CRET_VEG_ROUND=0 CRET_VEG_CREASE=0 CRET_VEG_EDGE=0
```

The global time runs on across a capture's shots, so compare runs with the
same `--shot` list. Fronds also sit a little differently before and after:
turning a card over (so it rounds outward) turns its flutter the other way.
The "after" runs here also had the sharper mips that were then dropped
(under 1/255 on average, section 2).

- `overview.jpg`: every shot, before and after. The pixels changed by more
  than 6/255: `vf_fern` 42%, `vf_forest` 19%, `dino_tp` 19%, `redwood` 9%,
  `gup` 5%, `ground` 3%, `canopy` 2%: the changes are in the near plants.
- `close_vf_fern.jpg`, `close_vf_forest.jpg`, `close_dino_tp.jpg`,
  `close_redwood.jpg`: crops at full size. Fronds and blades now shade light
  to dark across their width and read as folded; the fronds seen nearly edge
  on thin out instead of drawing a hard green line.
- `switches_vf_fern.jpg`, `switches_dino_tp.jpg`: one switch off at a time,
  from one run each with the same shot list, against all on and all off.

What to look at on your GPU: walk a hatchling through ferns and tall grass
and turn on the spot. Fronds turning edge on should thin and go, never draw
as a line, and not pop all together; nothing should flicker at a crown's
edge-on rim; backlit fronds against a low sun should glow from their shaded
face. Then compare with the three switches off.

## 5. Open questions

- **A 1024 atlas on ultra.** It would sharpen plants within about 2 m (and
  everything a hatchling sees at 0.5 m) for 168 MB more and about four times
  the atlas bake (cached after the first start). It needs larger sprites
  (`fetch_foliage.sh` caps them at 256-512 px; the ambientCG sources are 1K,
  the fern 2K) and the painter's pixel constants, written for 512, scaled.
  Worth it only if the close-up hatchling view matters enough.
- **Ground cover's up-mix.** `veg.fs` mixes 60% of a ground plant's normal
  toward straight up, which keeps ferns seen from below from drawing black
  but also flattens their rounding and any per-pixel normal detail. A
  smaller mix with a floor on the sky light might keep both; it needs your
  eye on a GPU.
- **Crease on crown leaves.** Crown cards are twig clusters of several leaves,
  so a fold along the twig would not follow any leaf; they only get the
  rounding and the fade.
