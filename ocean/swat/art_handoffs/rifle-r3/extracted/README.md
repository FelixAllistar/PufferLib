# Rifle 7 visual cleanup R3

Private offline art candidate based on immutable engine-tested R2. R3 is not yet runtime accepted. Original distribution rights remain unchanged; no public redistribution.

## Scoped changes
- Rear sight stalk: three-segment 0.35 mm bevel on eligible sharp edges
- Rear sight base: three-segment 0.40 mm bevel
- Upper receiver: three-segment 0.45 mm bevel, only edges with both endpoints forward of engine X=0.315 m
- Thin source features shorter than three times the bevel width are protected to avoid ineffective global overlap-clamping. Original R2 ring, aperture boundary, rail geometry, lower receiver, grip, trigger/guard, fore-end contact geometry, magazine and stock remain unchanged
- Scoped components use weighted broad-face normals and modifier-interpolated UVs. New tangent frames are orthonormalized. All existing R2 vertex-attribute arrays remain an exact prefix; 10,717 body triangle rows remain exact. 516 original scoped triangles are replaced

## Material treatment
Derived only from R2 4096-square base and ORM maps. On rear sight base and upper receiver, bright contrast above linear 0.025 retains 65 percent of the prior contrast, and roughness has a 0.73 floor. The scope mask comes from original source triangle UVs with a four-pixel gutter, then excludes every other active UV region, including magazine and hand-contact surfaces. All texels outside the resulting mask are exact R2 pixels; metallic/AO channels are unchanged everywhere. The original normal PNG remains byte-identical with scale 0.65. No remap, new UV set, extra material or extra draw primitive.

## Frozen invariants
Two unskinned meshes, magazine index 1; no animation or object-frame change. Overall rendered bounds, measured stock/muzzle/sight centers and hand contacts stay exact. The complete R2 ring, including its outer contour, both depth boundaries and octagonal inner opening, retains identical triangle indices, vertex positions, normals, UVs and tangents. Stock-to-muzzle length remains 0.901056695457287 m.

## Three hash-bound exports
- rifle7_visual_cleanup_r3.glb: 32,252,384 bytes; SHA-256 011f758561ead2519e7cf37f6b1379479e232dbf493d374e3ae06ef01b33133a; 21,112 exported vertices / 14,762 triangles
- rifle7_geometry_only_r3.glb: 35,981,128 bytes; SHA-256 ced35b875fe8cb35688f9b6b8854ce004b0cb7b126503b8b35e9dbdb9a3f12e8; 21,112 exported vertices / 14,762 triangles
- rifle7_material_only_r3.glb: 32,035,964 bytes; SHA-256 9da8e248900874020e0322ff85a1c94d59c740a18721a194d13538a81c382d37; 16,947 exported vertices / 12,013 triangles

Main and geometry-only variants add 4,165 vertices and 2,749 triangles versus R2. The material-only fallback is exact R2 geometry, including its rounded ring.

## Visual QA and known limits
Comparison files use identical R2/R3 camera, lights, AgX, -1.5 exposure and render settings. The four-way receiver sheet separates geometry and material contributions. Optical view is offline at measured sight direction and 120 mm eye relief, not the actual engine camera. The actual provided R2 engine sheet is retained as source context.

The stalk remains the same basic shape; this is restrained edge refinement. The functional inner aperture remains octagonal. Other rails, controls and lower receiver remain angular, and some source wear/scratches remain. The receiver improvement is intentionally subtle and excludes the rear grip-side edge zone. The new geometry costs 2,749 triangles; engine review should judge whether the visual gain merits that cost. Runtime visibility, contact clearances, mipmapping and lighting still require the engine owner's R3 checks.

Measured nearest-triangle distances at all new/original vertices are reported in SURFACE_DISTANCE_QA.json, not claimed as a continuous Hausdorff bound. Maximum sampled surface departure is about 0.486 mm; the stalk's cosmetic top bound is about 0.109 mm lower after corner trimming, with the overlapping ring and optical center untouched.

## Source and reproduction
Use the root-level inspect_components_r3.py, author_r3_bevels.py, assemble_r3.py, author_r3_finish.py, measure_r3_surfaces.py, validate_r3.py and render_r3.py scripts in that order. Blender 4.3.2, NumPy and Pillow were used. The modifier-based scoped Blender scene and packed full-rifle review scene are included. The source geometry NPZs, exact R2 GLB/maps and original rifle source are preserved separately and included in recovery packaging. R2 sealed files are unchanged.
