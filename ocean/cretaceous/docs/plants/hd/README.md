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

**Settings.** Settings, Display, **Plant detail** opens a page with two
switches, saved in `settings.ini` as `plant_detail` and applied at once:

| switch | what it does |
|---|---|
| Leaf surface detail | the normal tilt and cavity above |
| Light through leaves | the translucency above |

`CRET_VEG_DETAIL=0..3` (bit 1 surface, bit 2 light) overrides the saved
setting, for captures.

## 2. Pictures

DETAILS_PICTURES

## 3. Cost

DETAILS_COST

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

DETAILS_GPU
