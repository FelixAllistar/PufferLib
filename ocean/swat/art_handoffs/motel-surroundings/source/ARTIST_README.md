# Briar Court v3 appearance candidate

Separate source-art candidate, not native-engine accepted. Frozen solid geometry, collision and placement contract from v2materials/v1. No engine edits. Native screenshot in qa/native_reference/ motivates this pass; all generated previews are OFFLINE Blender Cycles GLB reimports, not native screenshots.

## Coordinated single trial

- Gravel baseColorFactor: [0.65, 0.65, 0.65, 1]. Stone: [0.78, 0.78, 0.78, 1]. These multiply decoded linear base color; they are not sRGB pixel edits. They serialize with normal float32 residuals. Both are native glTF PBR factors, not baked images or shader extensions. Native calibration is pending.
- All original image bytes stay unchanged. Base color is sRGB; pure roughness/OpenGL normal maps are Non-Color. Normal scale stays 0.65 for scanned surfaces and 0.45 for foliage; roughness factor 1, metallic 0, alpha OPAQUE. No added AO, shadows, displacement or alpha cutouts.
- Gravel retains 8/3 m repeat (three repeats/8 m module); bank 2 m; rocks approximately 1.8 m. Solid UVs unchanged from v2. Shoulder seams retain identical phase. The hard shoulder/bank material boundary remains.
- Five shrubs retain pinned root anchors and each original measured envelope. Lower branching, larger redistributed leaves and uneven lobes give bushier, asymmetric forms. Existing 1,265 closed leaves and 11 branches per shrub grouping are reused in budget, not added to. Foliage UVs regenerate; texture bytes/material bindings remain unchanged. This five-shrub grouping still repeats on every bank; no unique per-placement scatter is claimed.
- Rock surface triangles, normals and UVs remain exact. Material darkening does not change actual rock silhouettes. A later chipped-outline revision must supply a separately measured render + closed-collision pair for coordinated integration.

## Coordinated connected-ground material candidate

The explicitly requested actual connected_ground_render.glb is included separately beside the perimeter runtime. Only material CC0_Gravel_ground_01_8over3m baseColorFactor changes to [0.65,0.65,0.65,1], matching the shoulder trial. Asphalt stays unchanged. Every original BIN byte and all other JSON values remain exact: all 54 mesh names/order, positions, normals, tangents, UVs, root transforms and material bindings are preserved. The original accepted ground kit is untouched.

runtime/connected_ground_collision.glb and connected_ground_placement_manifest.json are frozen copies; the latter is separately named to avoid replacing the perimeter manifest. The reported engine support_matrix mapping remains index i -> 1119+i. This mapping and previous engine passes are supplied integration context, not fresh native testing of this candidate. No road geometry, corridor, clear approach or support change is made. Use the candidate ground render instead of, never alongside, the original ground render.

source/connected_ground.blend is a separate packed editable material candidate; ground-specific scripts, exact maps, provenance and preservation measurements accompany it. optional_ground_material/ supplies the exact JSON-only recipe and binding explanation, now applied to the included separate candidate. Ground QA records baseline/candidate hashes and verifies runtime/source preservation. No scalar alternatives or duplicate catalog variants are supplied.

Ground remains 54 meshes / 648 triangles. Substitution adds no geometry to the accepted layout. Perimeter plus ground is 62,888 triangles and 79 primitives before engine batching, with shared gravel scan bytes but separately named material records. No engine texture deduplication, mip policy or GPU-memory saving is assumed.

## Immutable contracts

runtime/bank_collision.glb, shoulder_collision.glb and placement_manifest.json are copied byte-for-byte from v2. Source build never re-exports collision or overwrites the manifest. The editable .blend retains original closed proxy meshes. Soil and rock render POSITION/NORMAL/triangle-index/UV data remain frozen. Only visual-only woody/leaf geometry changes.

East shoulder X=24, bank X=27; Z=-8,0,8. West shoulder X=-24, bank X=-27; Z=-4,4, rotation +pi around Y. Identity asset roots, no mirrored winding. Banks stay within their 3x8 m footprint; no shrub exceeds its original envelope. Shoulder stays 6x8 m, its court-facing top Y=-0.08. No front closure, fence-end or clear-approach alteration.

Ten semantic SOIL owners and 65 individual closed rocks across five placements. Numeric owners remain engine-owned, preserving existing owner 0 and accepted assets. Never generate collision from render foliage or combine soil/rocks into one convex hull. Use either the combined five_placements_render.glb or separately instantiated bank/shoulder renders, never both.

## Cost and evidence

Perimeter only: 62,240 render triangles, 25 primitives across five placements, five unique materials: unchanged triangle/draw-group count. Foliage contributes 55,400 triangles. Collision stays 75 closed components and 6,840 triangles. These are measured geometry budgets, not performance acceptance. Texture resolution stays three scanned 1K sets plus two original 512 sets. GPU residency, native compression/mips, overdraw and frame time are unmeasured.

qa/independent_report.json checks exported topology, render/collision equality, bounds, support, six module seams, materials and clean Blender reimport. Additional preservation/anchor checks compare against untouched v2. source/shrub_anchor_audit.json records original and candidate per-shrub envelopes and pinned roots. The measured anchor snapshot covers colliders, soil, owners and placements. Ground-specific audits separately preserve the established 54-mesh contract.

preview/ contains 12 matched baseline v2/candidate v3 perimeter camera images plus four coordinated-ground comparison images. Perimeter camera pairs in neutral and brighter outdoor light. Cameras are player-height, module close and west seam. Neutral exposure 0/sun energy 1; brighter outdoor exposure 1/sun energy 2. Both use AgX, the same fixed world/area-light setup and 24 samples. These bracket appearance for review; they do not reproduce the native engine renderer or prove final material brightness. qa/matched_cameras.json and qa/render_comparison.py record the setup.

Native integration must review the supplied view, close/grazing views, silhouettes, east/west joins, unchanged routes/owners and collision contacts, texture color-space interpretation, leaf aliasing/mips and actual performance. Do not accept the candidate based only on offline pictures.

## Rebuild

Blender 4.3.2, Python 3, NumPy and Pillow. Source .blend embeds textures. Original PNGs, metadata and provenance are retained for rebuilding.

From this folder:

    SKIP_RENDERS=1 blender -b -t 4 --python source/build_kit.py
    python qa/independent_qa.py
    blender -b -t 6 --python qa/render_comparison.py
    blender -b -t 6 --python qa/render_coordinated_ground.py

Frozen runtime collision/manifest inputs must be retained. Comparison checks additionally require untouched sibling motel_surroundings_kit_v2materials. No network download is needed. Do not run the v1 procedural texture generator on this candidate.

## Licenses and backup

LICENSE.txt explicitly covers original v3 contributions; licenses/baseline_provenance/ retains unchanged notices for prior original perimeter assets and Poly Haven CC0 scanned inputs. source/SCAN_PROVENANCE.json lists all original 1K/2K downloads and hashes. Connected-ground originals and asphalt scan provenance are also retained in licenses/ground_baseline/. No font or font file is used or bundled. Native screenshot is user-provided reference evidence, excluded from the new art dedication. Tools/dependencies are not relicensed.

packages/ provides separate runtime, review and editable source backups and a restoration/checksum inventory. Split parts must be concatenated in numeric order before extraction. Packaging does not upload, share, replace accepted assets or imply native acceptance.
