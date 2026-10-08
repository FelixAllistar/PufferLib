# Desk 023 oak finish W1: separate material comparison

This is an isolated render-material comparison, not a replacement for any currently pinned asset. Do not overwrite or bind it over the current runtime without a separate integration decision.

## Scope
- Existing owner: `REUSE__desk__023`; asset `desk`; catalog design `env:01_house/desk`.
- Local desk envelope remains 1.42 × 0.83 × 0.7523 m. 580 triangles unchanged.
- Thirty existing slab faces formerly assigned `HP_wood_faded` receive the separately named oak material. Lower pieces that share that source slot remain untouched.
- The original mesh has six users. Only desk 023 receives a copied mesh; no geometry is changed. The other five users retain their original data/materials.
- Existing scratch/detail geometry, transforms, parent, custom properties and collision ownership are preserved. This is not a broader geometry or furniture redesign.

## Source and physical scale
The supplied original Poly Haven Oak Veneer 01 diffuse, OpenGL normal and roughness PNGs are retained under `source_materials/polyhaven_oak_veneer_01/`. All are 1024 × 1024. Author: Jenelle van Heerden / Poly Haven. Source and CC0 license links are in the untouched SOURCE.json.

Archive paths were checked against traversal, absolute paths, links and special files. All supplied SHA256 and byte counts pass. The package contains no published MD5 fields; computed MD5s are recorded but are not an independent published-MD5 verification.

Use the supplied 1.8 m physical texture width. This supersedes the earlier unimplemented 1.83 m proposal. Original planar UVs were 2.3 units per metre; selected slab UVs are rotated and divided by 2.3 × 1.8, then translated by 0.5. Top grain runs along the local long X axis. The scale is baked into selected UV loops, so no runtime shader mapping dependency is required.

No source pixels are recolored, resized or synthesized. Diffuse is sRGB; normal and roughness are Non-Color. Normal shader amplitude is 0.3, using glTF normal scale; metalness is 0. Runtime metallic/roughness packing and any image encoding conversions are exporter-derived data, not original scanned maps. Inspect EXPORT_VALIDATION.json for embedded image hashes and channel routing.

## Rebuild
Requires Blender with bundled glTF exporter (tested version is recorded in QA).

From this directory:

    blender -b -t 6 --python build_compare.py -- --source /path/to/room101_v4_source.blend --render

Pass `--source /absolute/path/to/room101_v4_source.blend` to use the existing pinned v4 source. SOURCE_DEPENDENCY.json identifies its verified Drive folder, recovery package, and exact scene SHA256. The full room is not duplicated in the delta package. The build verifies that SHA256, builds the isolated comparison and writes matched before/after room and close-up captures. The saved comparison .blend packs the images. No engine code is involved. Source-light renders are review evidence only, not native engine lighting/GI parity.

After approving the visual result locally:

    blender -b -t 6 --python build_compare.py -- --source /path/to/room101_v4_source.blend --export

The export uses original identity local datum; apply the existing instance transform once. It exports a complete render mesh for the one owner, not merely the thirty changed faces. Keep the existing compiled collider, AI, damage ID and parent authority. Never draw both original and comparison render meshes simultaneously.

Native engine visual verification, performance, collision/AI behavior and any binding change remain unperformed. This package does not authorize broader replacement or alter the pinned fallback.

`INSPECTION_AND_BLOCKER.json` is the preserved historical pre-supply inspection. Its blocked download status was superseded by the user-supplied archive and current SOURCE_VERIFICATION.json. No canceled request was retried.

`desk_023_oak_w1_asset_source.blend` is a compact Blender library containing the editable desk object, materials, mesh and packed image dependencies only. Append its object into Blender for isolated inspection; full room rebuilds use the pinned scene dependency above.
