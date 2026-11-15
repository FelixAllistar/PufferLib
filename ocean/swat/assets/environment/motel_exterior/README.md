# Briar Court exterior kit (presentation only)

`exterior_kit.glb` is one Blender-built GLB holding:

- **Material library** (`L_*` swatch meshes): tiled PBR basecolor / OpenGL
  normal / glTF metallicRoughness (R=1, G=roughness, B=metal) sets for
  `stucco`, `repair`, `concrete`, `concrete_dark`, `roof`, `cream`, `teal`,
  `ochre`. At load the exterior shell modules (`room_facade_4m`,
  `solid_wall_4m`, `end_wall_6m`, `flat_roof_4x6m`, `gallery_canopy_4m`,
  `gallery_walkway_4m`, `canopy_post`, `corner_pier`, `lobby_glazed_facade`,
  `service_doorway_4m`, `gallery_access_ramp`, the roadside sign and Room 101's
  v3/v4 shell files) borrow these maps for their `MC15_<name>` materials
  instead of the 192 px placeholder images. The flat roof's top deck
  (`MC15_concrete_dark`) and the canopy's ribbed sheet (`MC15_steel`) use
  `roof`. The motel_v1 GLBs, their UVs, hashes and compiled collision are
  unchanged.
- **Dressing** (`P_*` pieces, 1,968 unique triangles): K-style gutters and
  end caps (ochre front, galvanized rear), rectangular downspouts with splash
  blocks, canopy roof membrane, precast window sills, rear security wall packs,
  electric meters with conduit, bathroom exhaust hoods, canopy post base
  plates, rooftop condensing units, plumbing/mushroom vents, a caged lamp for
  the vending nook and gooseneck floodlights on the roadside sign.
  `placements.json` lists the 55 placements (6,244 drawn triangles) with
  their owner object tags.

Every piece is drawn as a child of an existing authoritative object (wall skin
section under its origin, roof, canopy, post or sign) through the ordinary
motel draw, shadow and cutaway paths. Pieces have **no collider, navigation,
cover, acoustics, sight or protocol representation**; they disappear with
their support. `SWAT_MOTEL_EXTERIOR=0` restores the previous look.

## Rebuild

```sh
sh ocean/swat/tools/blender/build_motel_exterior.sh [SOURCE_CACHE] [WORK_DIR]
```

1. `tools/blender/motel_exterior_textures.py` downloads (if absent) and
   MD5-verifies the source maps below, crops each to the physical size of one
   shell UV repeat (~0.91 m; the modules map ~1.1 UV/m), makes it seamless with
   a half-period cross-fade, flattens low-frequency blotches, scales normal
   relief, resamples to 1024/512 px and tints to the original Briar Court
   palette (means of the motel_v1 placeholder maps, slightly weathered).
2. `tools/blender/motel_exterior_kit.py` builds the geometry/materials and
   exports `exterior_kit.glb` + `placements.json` (Blender 4.2, deterministic).
3. `tools/import_motel_exterior.py` regenerates `motel_exterior_data.h`
   (piece mesh ranges in Raylib's node/primitive order, library material
   indices, placements, kit SHA-256).

## Sources and licenses

All textures derive from CC0 1.0 Poly Haven assets (https://polyhaven.com/license);
geometry, procedural maps and scripts are original and dedicated CC0 1.0.

| Library set | Poly Haven asset id | Author(s) | Physical size | Maps used (URL, MD5) |
| --- | --- | --- | --- | --- |
| stucco, repair | `beige_wall_002` (Beige Wall 002) | Dimitrios Savva (photography), Rico Cilliers (processing) | 3 m | [diff 4k](https://dl.polyhaven.org/file/ph-assets/Textures/png/4k/beige_wall_002/beige_wall_002_diff_4k.png) `d358d83c5639eea227a8490a7d5bee8e`, [nor_gl 4k](https://dl.polyhaven.org/file/ph-assets/Textures/png/4k/beige_wall_002/beige_wall_002_nor_gl_4k.png) `25af04982d94840912a42ff28d012ab3`, [rough 4k](https://dl.polyhaven.org/file/ph-assets/Textures/png/4k/beige_wall_002/beige_wall_002_rough_4k.png) `2bc869bbbc09fc38c870dd25a222fc80` |
| concrete, concrete_dark, roof | `concrete_floor_02` (Concrete Floor 02) | Rob Tuytel | 2 m | [diff 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/concrete_floor_02/concrete_floor_02_diff_2k.png) `e6328911fe7cd3a74f11486f06a5ad60`, [nor_gl 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/concrete_floor_02/concrete_floor_02_nor_gl_2k.png) `e7bbae9a0ec85c590b82ccdf5788c46d`, [rough 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/concrete_floor_02/concrete_floor_02_rough_2k.png) `130e55c56d8f1db1c1ed0cd7d160fa78` |
| cream | `painted_plaster_wall` (Painted Plaster Wall) | Amal Kumar | 2 m | [diff 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/painted_plaster_wall/painted_plaster_wall_diff_2k.png) `a59f31e4461b4e9385e88030e32280d9`, [nor_gl 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/painted_plaster_wall/painted_plaster_wall_nor_gl_2k.png) `2eba0da17c1d194537c1231cb412e310`, [rough 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/painted_plaster_wall/painted_plaster_wall_rough_2k.png) `ca7f611c7ccbc7604fab09c698a4d329` |
| teal, ochre (painted metal) | `green_metal_rust` (Green Metal Rust) | Rob Tuytel | 1 m | [diff 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/green_metal_rust/green_metal_rust_diff_2k.png) `d6b7e4ba43193c1a526bd943f52dc11c`, [nor_gl 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/green_metal_rust/green_metal_rust_nor_gl_2k.png) `7e6eb0c41507137b07c84a4f8956dcb4`, [rough 2k](https://dl.polyhaven.org/file/ph-assets/Textures/png/2k/green_metal_rust/green_metal_rust_rough_2k.png) `55a52167b516c4ae75af5c48b50d7132` |

Galvanized, black iron, HVAC paint, grille and lamp-glow maps are tiny
procedural images generated in the kit script (fixed seeds). Source downloads
are not committed; only the derived, embedded PNGs are.

## Budget

- File: `exterior_kit.glb` ~11 MB (16 embedded PNG maps: 6 × 1024², 10 × 512²; 4 tiny procedural maps).
- Texture memory (API-format RGB8 with full mip chains): 34.0 MiB for the kit.
  The overridden 192 px placeholders stay resident (owned by their models), < 1 MiB.
- Triangles: 1,968 unique dressing triangles; 6,244 drawn for all 55
  placements; the 8 library swatches (16 triangles) are never drawn.
