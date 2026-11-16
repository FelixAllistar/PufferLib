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

DETAILS_NOT_DONE

## 5. What to look at on your GPU

DETAILS_GPU
