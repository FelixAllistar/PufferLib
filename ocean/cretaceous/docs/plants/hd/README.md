# Plants: better scans and real leaf materials

You said the plant materials were bad and the pictures low resolution: up
close the fronds read as blurry stickers. This page says what changed, how
to compare it on your GPU, what it costs, and shows before and after from
fixed cameras. Branch `cret/plants-hd` (on top of `cret/plant-cards`). The
source survey, with every candidate and why it was taken or not, is
[`../sources.md`](../sources.md).

## 1. What changed

**Better scans, at real resolution.** `tools/fetch_foliage.sh` now takes
the ambientCG leaf sets at 2K (were 1K) and Poly Haven's fern_02 at 4k (was
2k), and caps sprites at 512 or 1024 px (were 256 or 512): a fern frond is
now 250-290 x 1024 px from the 4k scan (was 128-288 x 512 from the 2k one).
Three new CC0 scans:

- **Grass blades** from Poly Haven `grass_medium_02` (single real blades,
  green and dry) for tall grass and sedge, instead of ambientCG's strand
  sheets.
- **Metasequoia sprays** from Poly Haven `fir_tree_01` twigs: flat,
  two-ranked needle sprays, much closer to Metasequoia than the old
  Thuja-like scale-leaf spray (kept as the fallback).
- **Tree-fern pinnae** from one Poly Haven `jacaranda_tree` leaf (a
  bipinnate leaf with real pinnules), mixed with the fern fronds.

A large sprite is now box-filtered into the texels it lands on (it was
sampled bilinearly, which aliased once the sprites got bigger than their
footprint), so the bigger scans shrink sharp.

**Leaf materials.** Every atlas texel now also has a material, in a second
texture array beside the colour atlas, layer for layer:

- **Normal** (red and green): the scans' own normal maps (ambientCG
  NormalGL, Poly Haven nor_gl), turned with each sprite as it is stamped;
  bark has its Poly Haven normal maps too. Painted parts paint their own:
  stems and ropes are round, painted leaves domed with a raised midrib.
- **Light let through** (blue): from ambientCG's Scattering scans (the leaf
  photographed against the light) where they exist (LeafSet022 laurel,
  LeafSet025 lobed leaves); elsewhere worked out from the photo (veins,
  midribs and stems that stand out lighter than the blade around them,
  and brown dry parts, let less through). A thin blade lets all of the
  light through, as before; veins and stems less.

In the plant shader (`shaders/veg.fs`):

- Leaf and bark normals tilt by the material's normal in a frame from
  screen derivatives (the meshes carry no tangents). Ground cover's 60%
  up-mix (which keeps ferns from going black from below) happens first,
  so a fern on the floor still shades leaflet by leaflet.
- The scanned tilt alone moved 2-3% of pixels: under the wrapped, sky-heavy
  foliage light a 20 degree tilt hardly changes the shade. So leaves take
  1.6 times the scanned tilt, and where the surface turns steeply away (a
  leaflet's curled edge, the folds either side of a midrib, a bark furrow)
  a cavity term from the same tilt darkens it. That is what makes a frond
  read as leaflets instead of one flat shape. Both fade with distance on
  their own, as the mips average the tilts out.
- Transmission (the backlit glow) is multiplied by the light let through:
  backlit leaves glow in the blade with dark veins and stems.
- Everything stays matte: no specular.
- Impostors bake the tilted normals and the cavity in, so far trees keep
  the same look.

**Settings.** Settings, Display, **Plant detail** (below Plant cards, the
page for the earlier card looks) opens a page with two switches and an
All switch, saved in `settings.ini` as `plant_detail` and applied at once:

| switch | what it does |
|---|---|
| Leaf surface detail | the normal tilt and cavity above |
| Light through leaves | the translucency above |

`CRET_VEG_DETAIL=0..3` (bit 1 surface, bit 2 light) overrides the saved
setting, for captures.

## 2. Pictures

Rendered with the real renderer on Mesa's software rasterizer (llvmpipe
under Xvfb; there is no GPU in the sandbox), high preset, 1280x720, with
the real terrain textures and props. "Before" is `origin/5.0` with
`cret/plant-cards` merged, built from its own worktree with the old
foliage fetch; "after" is this branch with both Plant detail switches on.
Same cameras, same shot list, so wind and time match.

The cameras stand 1, 2, 5 and 15 m from one real plant (found with the
renderer's own scatter, `render/veg_place.h`):

- **fern**: a ground-fern floor (camera at 1 m, a fern's height),
- **conifer**: a conifer forest, looking at the trunk and lower crown,
- **cycad**: a cycad among tree ferns, looking up into the crown,
- **grass**: open fern prairie with tall grass and sedge.

```bash
CRET_SHOT_CAM="fern_1m:4811.6:1499.2:1.00:1.964:-0.562:60;fern_2m:4812.0:1498.2:1.00:1.963:-0.297:60;fern_5m:4813.1:1495.5:1.00:1.964:-0.108:60;fern_15m:4816.9:1486.2:1.00:1.963:-0.035:60;conifer_1m:4765.0:1691.2:1.80:-2.749:0.418:60;conifer_2m:4766.0:1691.6:1.80:-2.749:0.229:60;conifer_5m:4768.7:1692.7:1.80:-2.749:0.091:60;conifer_15m:4778.0:1696.6:1.80:-2.749:0.030:60;cycad_1m:2985.9:2989.5:1.70:2.749:0.647:60;cycad_2m:2986.8:2989.1:1.70:2.749:0.383:60;cycad_5m:2989.6:2988.0:1.70:2.749:0.186:60;cycad_15m:2998.8:2984.2:1.70:2.749:0.104:60;grass_1m:2555.3:3179.3:1.60:0.785:-0.855:60;grass_2m:2554.6:3178.6:1.60:0.785:-0.508:60;grass_5m:2552.5:3176.5:1.60:0.785:-0.213:60;grass_15m:2545.4:3169.4:1.60:0.785:-0.083:60" \
CRET_SHOT_VARIANTS="noon:0.5:0.3,gold:0.71:0.35" \
  ./build/cretaceous/cretaceous capture DIR --size 1280 720 --shot 'fern_*,conifer_*,cycad_*,grass_*'
# A/B on one build: add CRET_VEG_DETAIL=0 (both off), 1 (surface only), 2 (light only).
```

Each picture holds the four distances, before on the left, after on the
right: `fern_noon.jpg`, `fern_gold.jpg`, `conifer_noon.jpg`,
`conifer_gold.jpg`, `cycad_noon.jpg`, `cycad_gold.jpg`, `grass_noon.jpg`,
`grass_gold.jpg`. `close_*.jpg` are full-size crops of the middle of the
1 m shots. `settings_plant_detail.jpg` is the new Settings page.

Share of pixels that changed by more than 6/255 (noon / golden hour):

| scene | 1 m | 2 m | 5 m | 15 m |
|---|---|---|---|---|
| fern floor | 63% / 52% | 48% / 42% | 41% / 39% | 43% / 39% |
| conifer forest | 5% / 4% | 7% / 3% | 7% / 3% | 11% / 4% |
| cycad and tree ferns | 12% / 10% | 9% / 7% | 11% / 7% | 37% / 21% |
| fern prairie and grass | 50% / 33% | 44% / 37% | 17% / 24% | 20% / 24% |

What I see in them:

- **Ferns** change the most. Up close each frond now reads as separate
  leaflets with darker folds between them and a lit rachis, where before
  it was one flat bright-green shape; the dry fronds show their pinnae
  instead of a pale paper cut-out. At golden hour backlit fronds glow
  between dark veins. The fern floor is a little deeper in tone (about 5%
  darker on average at 1 m, from the folds), not regraded.
- **Grass and sedge** are now real blades with a midrib and curl, from the
  grass scan; the prairie ferns read as ferns at 1-2 m.
- **Conifers** change little in this stand: its crowns are not the
  Metasequoia ones that took the fir sprays, and the bark already was a
  photo; its normal map only adds a little furrow darkening.
- **Cycads and tree ferns** change modestly at 1-5 m (tree-fern pinnae now
  carry real pinnules and normals; the cycad pinnae are blade scans as
  before). The 15 m changes are mostly the shrubs in front.
- Two cameras ended up behind foreground plants at 15 m (the fern camera
  at fern height, the cycad camera behind a shrub): they show the near
  plants, not the subject.

## 3. Cost

Measured on llvmpipe under Xvfb, high preset, 1920x1080, with the game's
own counters over 8 frames after the warm-up:

```bash
CRET_PRESET=high CRET_GPU_PROFILE=1 CRET_GL_STATS=1 CRET_CAPTURE_FRAMES=8 CRET_SHOT_CAM="(as above)" \
CRET_SHOT_VARIANTS="noon:0.5:0.3" CRET_VEG_DETAIL=3 \
  ./build/cretaceous/cretaceous capture DIR --size 1920 1080 --shot fern_2m,conifer_5m,cycad_5m,grass_2m
```
 "Before" is the before tree, "off" and "on"
this branch with `CRET_VEG_DETAIL=0` and `3`. llvmpipe's milliseconds are a
CPU rasterizer's: good for comparing runs, noisy by several percent ("off"
against "before" moves -8% to +6% with no shading change at all).

| shot | whole frame ms, before / off / on | before to on | plant passes ms, before / on | draws, triangles | fragments before / on |
|---|---|---|---|---|---|
| `fern_2m` | 2336 / 2142 / 2300 | -1.5% | 1363 / 1354 | same | 23.44 / 23.79 M |
| `conifer_5m` | 3051 / 3224 / 3224 | +5.7% | 1856 / 1979 | same | 31.37 / 31.38 M |
| `cycad_5m` | 1372 / 1456 / 1395 | +1.6% | 876 / 898 | same | 18.68 / 18.72 M |
| `grass_2m` | 1193 / 1226 / 1254 | +5.1% | 236 / 253 | same | 16.13 / 16.12 M |

So the whole frame stays within the 10% budget (-1.5% to +5.7%). Per
pixel the plant colour shader adds one texture fetch (the material
atlas, same coordinates as the colour atlas), the tangent frame from
derivatives and a few multiplies; nothing changes in the depth, shadow or
impostor passes. Meshes are untouched.

**Memory.** One new texture: the material atlas, RGBA8, 42 layers of
512x512 with mips, **59 MB** of GPU memory (the colour atlas is the same
size and unchanged). Peak process memory on llvmpipe (which keeps
textures in RAM) went from 1.40 to 1.47 GB in the 1080p runs.

**Atlas cache.** `build/cretaceous/cache/veg_atlas_mat.bin` is **59 MB**
beside the unchanged 59 MB `veg_atlas.bin`; both are rebuilt once when the
painting code or the scans change.

**Load time** (`CRET_PROFILE_LOAD=1`, plant stages, llvmpipe machine):

| | before | after |
|---|---|---|
| atlas from cache (every start) | 0.11 s | 0.24 s |
| atlas built (first start or after a change) | 1.15 s (sprites 0.39, painting 0.29, mips 0.48) | 4.67 s (sprites 2.50, painting 1.98, mips 0.20) |
| impostor bake (every start) | 1.8-1.9 s | 2.2-2.4 s |
| whole plant setup, warm cache | 5.84 s total | 6.58 s total |

The one-time build is slower because the sprites are larger PNGs with a
material file each (decoded on one thread) and are box-filtered into the
atlas. Every later start pays 0.13 s more for the second cached texture
and about 0.3-0.5 s more in the impostor bake (it now shades with the leaf
normals, on llvmpipe; a GPU does this in a fraction of that).

**Downloads.** `fetch_foliage.sh` downloads about 260 MB the first time
(kept in `build/cret_foliage/dl`) and writes about 55 MB into
`assets/foliage` (was 7 MB).

**Tests** (run step by step on this branch after merging the latest
5.0). Every `make test` step passes except `ecoprobe`: 137 of 146 results
are within budget. It fails with exactly the same numbers on the untouched
before tree, and it runs only the simulation, which this branch does not
touch. So `make test` as a whole still stops there. Web shaders compile (`web_shader_check.py`, 42 of
42).

## 4. Things I looked at and did not do

- **A 1024 atlas on high and ultra.** Not done. The atlas layer is 512
  texels; a fern frond card is about 1 m long, so at 1 m it gives about
  half a texel per pixel at 1080p (magnified about two times, slightly
  soft), at 2 m one to one, and from 3 m on it is minified. 1024 would
  only sharpen what is within 2 m (a hatchling's view), and it would cost
  four times the memory for both atlases: 235 MB of colour and 235 MB of
  material instead of 59 + 59 MB, and about four times the one-time atlas
  build. It also needs the painter's pixel constants (written for 512)
  scaled throughout `render/veg.c`. In the 1 m captures the softness is
  there, but what made the fronds read as stickers was the flat shading,
  which the materials fix. Worth revisiting only if the hatchling's
  close-up view matters that much.
- **A scanned 3D plant as the full-detail level of detail.** Not done:
  the CC0 plant models are themselves alpha-tested cards on a few stems,
  one fixed specimen each, so every fern in a stand would be the same
  plant, without our per-plant variation, wind, trampling, impostor bake
  or cross-fade. Their textures and normal maps are what we take
  (`../sources.md` has the full reasoning).
- **Cycads, palms, ginkgo, horsetails, araucaria.** No CC0 leaf scan of
  any of them exists on Poly Haven or ambientCG; they keep their stand-ins
  (blade scans, painted fans and ropes), now with normals. The best next
  step is baking cards from the CC0 Sketchfab scans of Metasequoia,
  ginkgo, horsetail, Osmunda and Dicranopteris (`../sources.md`).
- **Roughness and specular.** Not used: plants stay matte, as asked.
- **Compressing the material atlas** (BC5 for the normal plus BC4 for the
  light let through) would halve its 59 MB; the texture cache already
  builds BC3 for other textures. Left for a real GPU check first.

## 5. What to look at on your GPU

- Walk a hatchling through a fern floor and the prairie at noon and at
  golden hour, then switch **Settings, Display, Plant detail** off and on:
  the fronds should read as leaflets, not as one flat shape, and backlit
  fronds should glow between dark veins.
- Look for shimmer: the tilt and cavity come from the mips, so they
  should fade with distance, not sparkle. Mesa's llvmpipe filters
  textures like a GPU, but check anisotropic filtering at grazing angles.
- If the fern floor reads too dark to you, the cavity strength is one
  number in `shaders/veg.fs` (0.5) and the leaf tilt gain another
  (`LEAF_TILT_GAIN`, 1.6); the look-studio's colour controls sit on top.
- The frame cost on a real GPU: run the cost command above with
  `CRET_VEG_DETAIL=0` and `3`.
