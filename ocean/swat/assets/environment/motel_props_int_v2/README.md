# Briar Court interior props, layer v2

Presentation-only props for the motel guest rooms, the office/reception and
the laundry, placed by `ocean/swat/motel_props_interior_v2_data.h`
(`SWAT_PROPS_INTERIOR_V2=0` switches the whole layer off). Every model is
original geometry built by a deterministic Blender 4.2 script in
`ocean/swat/tools/blender/props_int_v2_<prop>.py` (shared helpers in
`props_int_v2_common.py`); rerunning a script reproduces the GLB bytes.
Each prop was hill-climbed against a photograph of the real thing; the log is
`ocean/swat/reports/cloud/art-props-interior/LOG.md`, the contact sheets
(reference | first render | final render | in-game) sit next to it.

Units are metres, +Y up, model front toward local +Z. Floor pieces: origin on
the floor at the centre of the back edge. Wall pieces (drapes, closet,
brochure rack): origin on the wall plane. Tabletop pieces (ashtray, coffee
maker): origin at the centre of the base.

```sh
cd ocean/swat
blender -b --factory-startup --python-exit-code 1 \
  -P tools/blender/props_int_v2_<prop>.py -- assets/environment/motel_props_int_v2 [--render PREVIEW.png]
```

Texture maps downloaded by `polyhaven()` are cached outside the repository
(`~/.cache/swat_polyhaven`, override with `SWAT_PH_CACHE`).

## Props, references and licences

| GLB | Tris | Mat. | Embedded maps (px) | Bytes | Reference photograph (licence) | Texture sources |
|---|---|---|---|---|---|---|
| drapes.glb, drapes_avocado.glb | 5,060 each | 2 | 512, 512 | 569,968 / 561,908 | John Margolies, "Fallsview Room 293, Ellenville, New York", LOC LCCN2017713322 — public domain | Poly Haven `floral_jacquard` diffuse + normal (CC0), recoloured gold / avocado |
| dresser.glb | 1,440 | 5 | 1024, 256 | 747,920 | John Margolies, "Bavarian Manor, room, motel annex, Purling, New York", LOC LCCN2017712643 — public domain | Poly Haven `american_walnut_veneer` (CC0); baked mirror map |
| mini_fridge.glb | 732 | 6 | 512 | 268,724 | "Small refrigerator" by theslowlane, Flickr 9373843725 — CC BY 2.0 | Poly Haven `american_walnut_veneer` (CC0) |
| coffee_maker.glb | 1,064 | 7 | – | 66,956 | "080202 mr coffee jr" by Dan4th, Flickr 2294690862 — CC BY 2.0 (shape only; no name or logo reproduced) | flat factors |
| ashtray.glb | 1,980 | 4 | 512 | 282,660 | "Blue Bird Motel and Restaurant Ashtray - Valparaiso, Indiana" by Shook Photos, Flickr 32572608418 — CC BY 2.0 (shape/finish only; print is abstract glyph blocks) | baked numpy map |
| wall_heater.glb | 380 | 4 | 256 | 70,748 | HABS CA-2139 photo 11 (LOC ca2139.photos.315142p), electric wall heater — public domain | baked numpy map |
| closet_rod.glb | 1,408 | 5 | 128, 128 | 100,600 | John Margolies, "Homowack Room 317 closet, Mamakating, New York", LOC LCCN2017712915 — public domain | baked numpy maps |
| waiting_chairs.glb | 5,372 | 4 | 256 | 262,932 | beam-seating photo by bfick, Flickr 3214261837 — CC BY 2.0 | Poly Haven `fabric_leather_02` (CC0), regraded to avocado vinyl |
| potted_plant.glb | 3,578 | 4 | 256, 256 | 228,604 | snake plant by Bigul Malayi, WordPress Photo Directory 358687d150 — CC0 | baked numpy maps |
| water_cooler.glb | 2,108 | 7 | 256 | 137,952 | "Watercooler Wikinews.jpg" by Jason Pratt — CC BY 1.0 | baked numpy map |
| brochure_rack.glb | 354 | 3 | 512, 512 | 462,884 | "SM Brochure Rack" by vastateparksstaff (Virginia State Parks), Flickr 50236900808 — CC BY 2.0 | Poly Haven `oak_veneer_01` (CC0); original made-up brochure covers |
| laundry_cart.glb | 6,448 | 4 | 512 | 517,872 | "LinenCart 20221105 101100.jpg" by Ka23 13, Wikimedia Commons — CC BY-SA 4.0 (linked only, not redistributed) | Poly Haven `rough_linen` (CC0) |
| linen_shelf.glb | 7,312 | 6 | 512, 512 | 685,740 | HABS IN-301-B-24, linen storage room, Grissom AFB Building 747 — public domain | Poly Haven `plywood`, `terry_cloth` (CC0) |

Total: 14 GLBs, 43,756 triangles, 4,965,468 bytes. Textures are 1K at most
(the dresser veneer) and mostly 256–512 px.

The reference photographs are used only to judge shape, proportion, material
and value; no pixels from them are in the models. Copies of the public-domain,
CC0 and CC BY references (downscaled, ≤ 1280 px) are committed in
`ocean/swat/reports/cloud/art-props-interior/refs/` with the credits above; the
CC BY-SA laundry-cart photo is linked, not copied. Full per-prop notes are in
`ocean/swat/reports/cloud/art-props-interior/log/<prop>.prov.md`.

All models and baked maps in this folder are original work dedicated CC0 1.0;
the Poly Haven maps they embed are CC0 1.0 (https://polyhaven.com/license).
No brand names or logos are reproduced.
