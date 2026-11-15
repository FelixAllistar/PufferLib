# Briar Court interior pass (recipe v1)

Presentation-only interior art for the motel mission: bed replacement looks,
bathroom fixtures, guest clutter, reception/laundry dressing and four tileable
surface maps. Built deterministically (byte-identical on rebuild) by

```sh
blender -b --factory-startup --python-exit-code 1 \
  -P ocean/swat/tools/blender/motel_interior_v1.py -- ocean/swat/assets/environment/motel_interior_v1
```

Blender 4.2.3 LTS. `build_report.json` lists per-asset triangles, material
counts and engine-space bounds. Placement, ownership and fallback rules live in
`ocean/swat/motel_interior_art.h`; see ENVIRONMENT_ART.md, "Briar Court
interior pass". License: CC0 1.0, original procedural work (LICENSE.txt).

| Map | Size | Repeat |
|---|---|---|
| `wallpaper_stripe_damask_512.png` | 512² | 0.6 m (metric UVs) |
| `carpet_burgundy_medallion_512.png` | 512² | source floor UVs (~0.8 m) |
| `bath_tile_grout_512.png` | 512² | source floor UVs |
| `ceiling_popcorn_stained_512.png` | 512² | source roof UVs |

Embedded GLB images are 256² (quilts, veneer, print, cardboard, labels, fabric).

| File | SHA-256 (prefix) |
|---|---|
| `bath_mat.glb` | `a22be7346779e83b…` |
| `bath_mirror.glb` | `bdddb52a9b8347a8…` |
| `bed_bedding_rose.glb` | `da470003010a8e31…` |
| `bed_bedding_teal.glb` | `28eb5a5fbab5228f…` |
| `bed_frame.glb` | `809b34c50d25cf31…` |
| `counter_kit.glb` | `a024ecda8f98f7e5…` |
| `desk_bottles.glb` | `2a3bbd810dfd1cd2…` |
| `detergent.glb` | `d45b85476c66f53e…` |
| `floor_clothes.glb` | `b3dc3ade56ccd335…` |
| `laundry_basket.glb` | `219b1e65b4091d3c…` |
| `pizza_box.glb` | `ac91d1991c720207…` |
| `shower_curtain.glb` | `212d07691c15c09b…` |
| `takeout.glb` | `11640d69420d95c3…` |
| `towel_pair.glb` | `63e120d35f3f1945…` |
| `towel_rail.glb` | `fcf40a4cf597c728…` |
| `wall_clock.glb` | `5e1e91e1b2b6e910…` |
| `wall_print_desert.glb` | `ec643cbdfd740bb5…` |
| `wastebasket.glb` | `95c6cd439a58295b…` |
| `bath_tile_grout_512.png` | `25192c0727119a5d…` |
| `carpet_burgundy_medallion_512.png` | `ec4e6dec6ed51a96…` |
| `ceiling_popcorn_stained_512.png` | `12ff4e33a5f4f3b8…` |
| `wallpaper_stripe_damask_512.png` | `c958db6ffe5114d1…` |
