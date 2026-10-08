# Quiet Evidence · Household Clutter 02

28 original, worn household props for dressing the rooms of the existing house kit. Kitchen objects, bathroom odds and ends, and desk/personal clutter. The pack extends the existing 36-prop kit without repeating any of its asset types. All models use real metre scale, restrained aged materials, and visible modeled details rather than generic proxy boxes.

The final standalone library totals **73,640 triangles** and **12.54 MB across 28 GLBs**. The native scene uses 20 packed image textures.

## Start here

- `previews/contact_sheet.png` — labeled catalogue of all 28 assets; preview scale is normalized for legibility. Labels give actual dimensions and exported triangle counts.
- `previews/countertop_vignette.png` — staged countertop overview.
- `previews/countertop_detail.png` — close-up of small personal/bathroom objects.
- `modules/*.glb` — one portable asset per file, without lights, cameras or presentation furniture.
- `household_clutter_02.blend` — editable asset library, packed textures, linked countertop instances, lighting and ready-to-render camera.
- `manifest.json` — exact sizes, triangles, materials, bounds, embedded-image counts and SHA-256 hashes for every GLB.

The presentation counter/backboard is a display set in the native scene, not an additional reusable asset. No existing delivered house file has been modified.

## Inventory

Kitchen (10): enamel kettle; chipped coffee mug; stacked dinner plates; seasoned frying pan; lidded saucepan; two-slot toaster; perforated box grater; scarred cutting board; utensil crock with spoon, spatula and whisk; steel colander.

Bathroom (8): soap dish and bar; pump soap dispenser; squeezed toothpaste; worn toothbrush; unrolling toilet paper; tissue carton; folded hand towel; oval hairbrush.

Desk and personal (10): rotary telephone; twin-bell alarm clock; caged desk fan; portable AM/FM radio; television remote; red stapler; open sewing scissors; brass eyeglasses; house keys and tag; creased leather wallet.

## Visual treatment

Cream enamel, faded olive paint, blue/teal plastic, tobacco leather, tarnished steel/brass and natural wood. Original deterministic 256×256 base-colour images add grime, scratches, specks, grain and cloth weave. Macro details are modeled: open bowls, finger loops, dial recesses, grater/colander perforations, wire guards, crimped tube, towel folds, bristles, stitching and key teeth. Generic unbranded labels are included where useful for recognition. No third-party model, scan, brand/logo artwork or downloaded texture is included.

Each standalone GLB is one editable mesh with compact material slots, UV coordinates and normals. The phone cord, wire cage and some closely viewed details intentionally use more triangles. The pack has no LOD chain; use the manifest to choose economical scatter subsets. Batch meshes or instance where the renderer supports it. Detailed small props should not become independent gameplay actors by default.

## Units, pivots and placement

- Blender: right-handed, +Z up; canonical front is -Y.
- glTF: right-handed, +Y up; front is +Z. Export conversion is `(x,y,z) → (x,z,-y)`.
- Individual source origin: XY bounding-box centre and lowest mesh point at Z=0, for direct surface placement.
- Do not apply the axis conversion again to vertices imported from a GLB.
- The manifest includes both Blender-space and glTF-space dimensions and AABBs. Custom-property text and numbers are metadata; glTF does not automatically convert arbitrary extras.
- Small geometric details are deliberately millimetre scale. Avoid merging by an overly large vertex weld tolerance.

The canonical objects live in `01_ASSET_LIBRARY` at the origin and are hidden in the native viewport to prevent overlap. Use the Outliner to reveal a single source, or use its Blender Asset Browser entry. The rendered arrangement uses linked mesh instances in `02_COUNTERTOP_VIGNETTE`. All source objects remain unscaled; catalogue renders alone normalize display size.

## Materials and portability

All materials use standard glTF metallic/roughness PBR. Base-colour PNG images are packed in the native scene and embedded in each GLB, so the `.glb` needs no sidecar textures. The `textures/` folder retains the generated originals for rebuilding. No Blender noise shader, generated-coordinate mapping, external file path, transparency extension or transmission extension is needed at runtime.

Eyeglass lenses are opaque, lightly tinted PBR surfaces for broad renderer compatibility. Very thin decorative surfaces (tissue, some perforations and fan blades) are intentionally double-sided. The prop meshes are visual art, not watertight solids intended for automatic dynamic collision. The scissors and fan have no animation rig. The kettle lid and phone receiver are modeled as parts of the one asset mesh, not independently scripted interactions.

## Collision and PufferLib boundary

Default: render-only, no collider. Keep countertops clear of unnecessary micro-colliders. In particular, keys, papers/tissue, cords, towel, toothbrush, scissors and glasses should not disturb agent navigation or projectile collision.

If the game later promotes a prop to an interactive pickup, start with a coarse AABB from `manifest.json` or a deliberately authored convex hull. Size it for the desired gameplay behavior. The manifest bounds are guidance, not an installed collision shape. Avoid using the detailed triangle soup as a dynamic collider. Retest navigation and doors after adding any obstacle.

This is an art-only extension. It includes no PufferLib renderer/importer, material-ID mapping, entity binding, damage state, animation system, navigation edits, or game-source changes. Existing house `ENGINE_ADAPTER.md` remains authoritative for integration scope. Native/GLB tests do not prove in-game performance or appearance parity.

## Rebuild

Built and checked with Blender 4.3.2. The build is standalone; it does not import or overwrite the delivered house scripts.

```sh
cd environment_batches/02_household_clutter
blender --background --threads 2 --python-exit-code 1 --python build_clutter.py -- --all
python3 compose_catalog.py
python3 qa/validate_assets.py
blender --background --threads 2 --python-exit-code 1 --python qa/reimport_assets.py
```

`--all` rebuilds exports, the native scene, both vignette renders and all catalogue tiles. Without flags, only assets, manifest and native scene are rebuilt. `--render` also renders the two vignettes without the 28 catalogue tiles. Blender supplies `bpy`, `bmesh` and NumPy; the optional catalogue compositor needs Pillow and the system DejaVu font. Neither is needed to import assets.

Rebuilding intentionally replaces this batch's generated files. Save hand-edited copies separately first. Source/design-level reproduction is deterministic with seed 260204; byte-identical output across Blender versions is not promised. The 28 individual render tiles are retained in the working folder as build intermediates. The compact distribution includes their full contact sheet rather than duplicating those individual PNGs; run the full rebuild to regenerate them.

## QA

`qa/structure_report.json` checks every GLB's structure, finite positions, nondegenerate triangles, primitive indices, UVs/normals, embedded PNG payloads, material data, asset IDs and file hashes.

`qa/reimport_report.json` records fresh Blender imports of all 28 GLBs, verifies exported triangle counts and source dimensions, confirms metre-scale surface pivots, then reopens the native scene to check all packed images and 28 source assets.

`qa/rebuild_geometry_report.json` independently rebuilds every source mesh in memory and checks for zero-area triangles.

Preview images were inspected as artwork, and the final geometry was refined where necessary. These are asset-level tests only, not an engine benchmark, game playthrough, or cross-renderer shader comparison.

## Licence

Original geometry, generated texture artwork, scripts and rendered previews in this batch are dedicated under CC0 1.0 Universal. See `LICENSE.txt`. No attribution is required; the metadata is retained for provenance. This dedication does not relicense Blender, PufferLib or another engine.
