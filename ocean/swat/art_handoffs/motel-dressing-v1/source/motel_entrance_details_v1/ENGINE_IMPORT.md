# Motel entrance details v1 — visual-only proposal

Two original unbranded fixtures fill gaps in catalog r006 (322 unique designs, 30 LOD records). Thermostat and smoke alarm already exist and were deliberately skipped.

## Units, axes, pivots and ownership
- Source and glTF units: metres, scale 1. Source Blender +Z up, -Y toward viewer; runtime glTF +Y up, +Z outward. +X is fixture right. Root origin is the center of the rear mounting-contact plane. Each standalone root is at identity; source assets overlap at origin intentionally for independent export.
- me01_door_viewer_face: 0.0254 m diameter, 0.00635 m projection. Surface-only outer face of a door viewer. All three parts belong to its asset root; proposed parent is a door leaf. No hidden barrel, rear eyepiece, hole, live camera, optical shader, locking, or interaction. Opaque dark lens is intentional. An installer may attach it as an independent visual child at an appropriate door-local transform after reviewing the actual door; no existing door ID is embedded or altered.
- me01_concave_wall_bumper: 0.0635 m diameter, 0.0254 m projection. All five parts belong to its asset root; proposed parent is a fixed wall. Rubber cup has real recessed geometry. Position must be checked against the real handle swing path by the engine owner. This asset does not enforce contact or halt a door.
- Viewer reference installation height is 1.524 m; that is optional reference context, not an applied world-space placement. Bumper height is deliberately unspecified until the real door and handle are checked.
- No physics colliders, gameplay material IDs, destruction, animation, acoustic behavior, or engine files are authored. Room101 door and collision identifiers are untouched. The two parent categories must never be interchanged.

## PBR and LOD
512-square original seeded base-color, OpenGL tangent normal and packed roughness/metallic textures. UV0 repeat is exactly 0.25 m in each planar axis for every material; mapping is per-face dominant-axis projection from unscaled source coordinates. Curved silhouettes are smooth shaded; all surfaces are closed, single-sided geometry.

Base color uses sRGB, normal and RM use linear data. RM channels: R=255 unused; G=roughness; B=metallic. Factors are white/1/1. Lens is opaque and non-metallic. Rubber and gaskets are non-metallic. Satin hardware is metallic. AO is an independent baked image on uniquely packed UV1, CLAMP_TO_EDGE; it is not multiplied into the base color or packed into the RM image. UV0 PBR samplers repeat. Normal scale and AO strength are 1.

Two explicit GLBs per design, LOD0 and LOD1; not an automatic MSFT_lod hierarchy. LOD1 transfers source normals to the reduced body meshes, while retaining the small lens topology to keep deterministic output. Both are separate alternatives, never simultaneously rendered. Recommended ordinary-room-distance budget candidate: LOD1 for both fixtures (bumper 914 triangles; viewer 562). LOD0 (bumper 2,044; viewer 976) is an inspection / very-close-shot alternative, not the default budget recommendation. At only 63.5 mm and 25.4 mm diameter these details usually occupy few pixels; retaining LOD0 at normal room distances has little benefit. LOD1 trades some curvature regularity for approximately 55% fewer bumper triangles and 42% fewer viewer triangles. The reduced metal surfaces still show uneven specular highlights at macro viewing distance; source-normal transfer does not eliminate that limitation. Prefer LOD1 only when the fixture diameter is at most about 32 screen pixels, and use LOD0 for larger or closer presentation. The pixel guide is a proposed, unbenchmarked heuristic, not a proven switching threshold. Screen-size switching and eventual small-object culling need engine-owner tuning; no automatic distances or culling are implemented. No Xbox 360 hardware benchmark or art approval is claimed.

Each final GLB root records the actual exported variant triangle count, LOD level and runtime bounds. Never interpret its counts as LOD0 counts for both variants. Read MANIFEST.json for measured data.

## Rebuild
Requires Blender 4.3+ (verified installed version recorded in MANIFEST), Python 3, numpy, Pillow.

1. python make_textures.py
2. blender -b -t 4 --python build_props.py
3. python validate_glb.py
4. python extended_qa.py
5. blender -b -t 4 --python render_exports.py
6. blender -b -t 4 --python render_exports.py -- 1

Clean verification: blender -b -t 4 --python build_props.py -- --out /tmp/entrance-rebuild-verify
Then python check_rebuild.py . /tmp/entrance-rebuild-verify

The source .blend retains separately named owned parts, editable mesh data, both UV sets and linked portable texture paths. Runtime files are resealed after Blender export to guarantee independent UV1 AO and truthful variant metadata. The seal also canonicalizes triangle-index ordering without changing winding. Rebuilding is deterministic for runtime GLBs and generated/baked textures, not for timestamps in .blend.

## Preview scope
All five previews are neutral Blender renders after importing the shipped GLBs. The mount proof is two separate illustrative surface vignettes: viewer on green door finish, bumper on tan fixed wall finish. They show parent intent and are not a floor plan, game screenshot, validated room integration, or a claim that a bumper should sit next to the viewer in a real installation. Preview geometry is absent from runtime assets.

## QA scope
Independent binary accessor audit covers finite attributes, triangle count, transformed bounds, outward signed volume, welded watertight topology, normals, tangents, UV0 nondegeneracy, UV1 range, image dimensions, PBR/AO wiring and part ownership. Extended QA checks UV1 triangle area and raster interior-overlap at 512 texel resolution, plus exact runtime metadata versus geometry. Raster non-overlap is not a mathematical proof at infinite resolution. Clean rebuild results and image review are separate reports. No real-engine integration was performed.
