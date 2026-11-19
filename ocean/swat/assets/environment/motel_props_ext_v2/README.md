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
| BC2_VAN01.glb | mid-1960s forward-control panel van, warm grey | 3,728 | dirty_concrete (grain) | Commons "1965 Ford Econoline HD Van (54486012308).jpg" by Mustang Joe (CC0) |
| BC2_PALM01.glb | desert fan palm with dead-frond skirt, ~12 m | 7,722 | palm_tree_bark, dirty_concrete (grain) | Commons "Washingtonia filifera- oasis.jpg" by Thayne Tuason (CC BY 4.0) |
| BC2_WAGON01.glb | 1970s compact station wagon, dirty white | 3,192 | dirty_concrete (grain) | Commons "1970s Toyota Corolla estate (wagon) 1.2 Deluxe (E30) (10562484414).jpg" by Charlie from United Kingdom (CC BY 2.0) |
| BC2_PAYPHONE01.glb | pedestal payphone, faded yellow hood | 1,074 | dirty_concrete (grain) | Commons "Payphone Main St Moab UT 2025-05-16 11-54-11 1.jpg" by G. Edward Johnson (CC BY 4.0) |
| BC2_NEWSBOX01.glb | row of four newspaper vending boxes | 960 | rusty_painted_metal (grain) | Commons "Group of Newspaper Vending Machines.jpg" by Twzink (CC0) |
| BC2_TYRES01.glb | dumped tyre stacks with dry grass | 8,072 | dirty_concrete (grain) | Commons "Old tires stacked near a fence with grass growing on top.jpg" by Shixart1985 (CC BY 2.0) |
| BC2_MATTRESS01.glb | discarded quilted single mattress | 1,328 | own quilt map (CC0), dirty_concrete (grain) | Commons "Discarded mattress by trash bin (18822330575).jpg" by Eric Fischer (CC BY 2.0) |
| BC2_TUMBLE01.glb | dry tumbleweed, ~1 m | 7,776 | dirty_concrete (grain) | Commons "Tumbleweed 038.jpg" by ImperfectTommy / Edmond Meinfelder (CC BY 2.0) |
| BC2_JOSHUA01.glb | Joshua tree, ~7.5 m | 4,962 | palm_tree_bark, dirty_concrete (grain) | Commons "Joshua Tree (Yucca brevifolia) (51632975337).jpg" by Joshua Tree National Park (public domain) |

Licence of the generated files: CC0 1.0 (original work; Poly Haven inputs are
CC0). No brand logos, badges or real names.
