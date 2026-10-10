# Secondhand Rooms: furniture variants

12 distinct original environment assets for the grounded, muted, worn-house style of the initial kit. No character assets or changes to the initial house package. All geometry and texture artwork is CC0-1.0.

## Open and use

- `furniture_variants.blend`: editable source library. The 12 named Asset Browser objects are arranged in a 4 × 3 catalogue grid, one collection per asset. Each asset is a single editable mesh containing separate loose component islands. In Edit Mode, select linked parts to move or separate cushions, hardware, drawers, cloth or slats.
- `assets/*.glb`: one canonical standalone asset per file. These are the files to place in a level; every mesh, material and image dependency is embedded.
- `manifest.json`: exact local bounds, dimensions, triangle counts, surface semantics, optional coarse collision AABBs and footprints.
- `previews/00_contact_sheet.jpg`: labelled catalogue of actual Blender renders.
- `previews/01_reading_room.jpg`, `02_bedroom_storage.jpg`, `03_utility_corner.jpg`: staged views of the same meshes. Walls and floorboards in these views are presentation scenery and are not extra exported assets.
- `previews/<asset_id>.jpg`: full-size individual asset views.
- `textures/`: original 256/512px PNG texture sources, also packed into the native scene and embedded in GLBs.
- `qa/`: structural and clean-Blender round-trip reports.

## Asset catalogue

Dimensions are width X × depth Y × height Z in the canonical Blender frame. Bounds include handles, outstretched footrests and open components.

| Asset | Dimensions (m) | Triangles |
| --- | --- | ---: |
| Worn wingback armchair | 0.930 × 0.839 × 1.359 | 4,668 |
| Extended vinyl recliner | 1.095 × 1.713 × 1.312 | 4,986 |
| Metal folding chair | 0.508 × 0.629 × 0.876 | 1,202 |
| Spindle dining chair | 0.494 × 0.510 × 0.966 | 1,884 |
| Folding card table | 0.920 × 0.920 × 0.766 | 1,090 |
| Low six-drawer dresser | 1.410 × 0.781 × 0.841 | 2,186 |
| Tall panel wardrobe | 1.220 × 0.935 × 1.978 | 2,300 |
| Slatted shoe rack | 0.838 × 0.356 × 0.526 | 1,944 |
| Woven laundry hamper | 0.573 × 0.429 × 0.676 | 8,146 |
| Single institutional bedframe | 1.052 × 2.136 × 1.095 | 2,524 |
| Repaired steel shelving | 1.235 × 0.523 × 1.820 | 1,922 |
| Tambour-door credenza | 1.540 × 0.560 × 0.782 | 2,264 |

The wing chair has a tall winged back, turned feet and a repaired seat tear; the recliner exposes its static footrest mechanism. The spindle chair and folding chair have separate visual construction. The wardrobe includes an interior, the dresser has one ajar drawer, the wicker hamper is genuinely open, and the single institutional bed is a bare frame rather than another dressed double bed. The shoe rack includes one static pair of loafers; its footwear and the hamper cloth are separable loose parts in the source mesh.

## Scale, pivots and materials

- Source: metres, right-handed Z-up, canonical front -Y. The origin is an authored furniture-body reference in X/Y, not the centre of the complete AABB. Handles, ajar doors/drawers and extended footrests intentionally create asymmetric bounds. The lowest mesh vertex is normalized to Z=0; the manifest records any authoring Z adjustment. The source library's grid arrangement is for browsing only.
- GLB: right-handed Y-up, front +Z. Blender's exporter maps `(x, y, z)` to `(x, z, -y)`. Do not convert GLB vertices or imported node transforms twice.
- GLB node extras retain `asset_id`, `license`, `units`, `canonical_front`, `collision_footprint_xy` and collision/runtime status.
- `collision_footprint_xy` is always a canonical Blender-local XY four-number tuple `[min_x, min_y, max_x, max_y]`. Exporter conversion does not touch arbitrary custom-property payloads.
- Material slots carry surface semantics. Base colour uses UV-mapped PNGs; roughness and metalness use portable Principled scalar values. Textured materials export a white colour factor, avoiding accidental double tinting. No procedural shader, external texture link, alpha blend or material extension is required. All materials are exported double-sided, including the open liner and wire/cloth detail. This is portable but may add raster cost; review culling/material policy in the receiving renderer.
- The muted slate wool, olive fabric, tobacco vinyl, warm worn wood, sage/cream painted steel and subdued brass palette was designed for the existing house style. Close-up stitching, button details and thin wire geometry are visual dressing.

## Engine and collision boundary

This is an art batch, not an installed game feature. It does not modify the engine, renderer, navigation, mission grammar or gameplay object budget. The initial house kit's `ENGINE_ADAPTER.md` remains the broader integration reference.

For the previously inspected SWAT material enum, wood=2 and steel=4 are suggestions only. Cloth and rubber have no exact enum match: choose a deliberate gameplay mapping rather than deriving one from a texture name or assigning carpet automatically. Preserve the authoritative object's physics material, collision extents and state until an intentional integration changes them.

All exports are render-only by default. No collision meshes, triggers or rigid bodies are enabled. `optional_coarse_aabb_y_up` in the manifest is an explicitly converted optional proxy suggestion; it includes open doors, the extended footrest and decorative protrusions. A solid AABB around the hollow shelving/bed/wardrobe is deliberately conservative and may be unsuitable for gameplay. Review each placed piece before creating physics. Recheck door clearance, spawn areas and actor navigation after furniture placement. The initial adapter's 0.30 m grid and 0.43 m actor expansion are integration guidance, not a test performed by this batch.

The ajar doors/drawer, folding underframes, caster wheels and recliner mechanism are static geometry. No animation rig, articulated interaction, LOD chain, destructible binding, lightmap UV set or engine shader-parity/performance claim is included.

## Rebuild and verify

Built and tested with Blender 4.3.2. From this folder:

```sh
blender --background --threads 2 --python-exit-code 1 --python build_furniture.py
python3 validate_furniture.py
blender --background --threads 2 --python-exit-code 1 --python qa_reimport.py
blender --background --threads 2 --python-exit-code 1 --python render_furniture.py
python3 make_contact_sheet.py
python3 package_batch.py
```

The build and Blender QA use Blender's bundled Python, NumPy and glTF exporter. The contact-sheet compositor uses ordinary Python plus Pillow; it only lays out the actual render images and labels. The packaging script creates high-quality JPEG presentation copies and a compact ZIP. Lossless PNG render masters remain in the working folder and can be regenerated, but are omitted from the compact ZIP. Texture PNGs remain lossless in every delivery. Rendering uses CPU Cycles and does not require denoiser support. Scripts resolve paths relative to themselves and need no network or initial-kit dependency. A rebuild replaces generated exports and the native scene; preserve hand edits in a separate file first.

## Validation scope

Both structural GLB checks and clean Blender re-import checks passed for all 12 assets. The checks compare triangle counts and bounding boxes, inspect embedded images and metadata, and reject missing UVs, non-finite geometry or degenerate triangles. The native scene's image packing and metre scale are checked as well. Reports contain per-file SHA-256 values and apply only to the files present when checked. These are asset-data tests, not a playable-level, collision, draw-call, frame-rate or in-engine visual test.

The individual models and staged renders were visually reviewed for silhouettes, genuine openings, material readability and intersections. Packaging contains only the furniture batch and original texture sources; no unused downloads or initial-house duplication.
