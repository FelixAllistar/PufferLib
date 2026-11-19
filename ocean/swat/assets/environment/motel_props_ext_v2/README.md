# Briar Court exterior props v2

Presentation-only props for the motel lot, street and verges, drawn by
`motel_props_ext_v2_impl.h` from `motel_props_ext_v2_data.h` (generated from
`placements.json` by `tools/blender/props_ext_v2/pe2_placements.py`).
`SWAT_PROPS_EXT_V2=0` turns the whole layer off.

Every GLB is generated from code by `tools/blender/props_ext_v2/<prop>.py`
(Blender 4.2, `blender -b --factory-startup -P <prop>.py -- --export`), with
shared helpers in `pe2_common.py`. Geometry is original. Colour is a vertex
colour tint (display-encoded, as the engine's lit shader reads COLOR_0)
multiplied by a 512 px grain or material map processed from CC0 Poly Haven
textures (downloaded by `pe2_common.fetch`, URLs in `POLYHAVEN`). Reference
photos guided proportions only; nothing from them is in the assets.

| File | Prop | Triangles | Textures (CC0 Poly Haven) | Reference photo (licence) |
|---|---|---|---|---|
| BC2_PICKUP01.glb | late-1960s pickup, faded blue | 3,520 | dirty_concrete (grain) | Commons "Chevrolet c10 pickup argentina.jpg" (public domain) |

Licence of the generated files: CC0 1.0 (original work; Poly Haven inputs are
CC0). No brand logos, badges or real names.
