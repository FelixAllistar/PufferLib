# Briar Court: original surroundings kit v1

Two authored modules: a **6 m outward × 8 m longitudinal gravel/soil shoulder** and a **3 m outward × 8 m longitudinal scrub bank**. Render meshes, closed collision proxies, deterministic source and offline GLB round-trip previews only. No engine implementation or native integration claim.

## Placement contract

Metres, Y up. Local +X points outward; local +Z is longitudinal. All exported asset geometry uses identity roots. `runtime/placement_manifest.json` is authoritative: five semantic placements (`east_0`, `east_1`, `east_2`, `west_0`, `west_1`), each with a shoulder and bank.

East shoulders start at X=24, centered Z=-8,0,8; banks start X=27. West shoulders start X=-24, centered Z=-4,4, rotated **+π about Y**; banks start X=-27 with the same rotation. This produces east X=[24,30], west X=[-30,-24], banks in outer halves [27,30] and [-30,-27]. Row-major matrices multiply column vectors. West local +Z maps to world -Z. Do not mirror vertex coordinates or reverse winding at import.

The court-facing shoulder edge is X(local)=0, Y=-0.08 along its full length. Adjacent end profiles match. Shoulder top grades gradually from -0.08 to -0.20; the closed bottom at -0.46 provides real solid thickness. It does not extend inside the court. Bank closed geometry remains Y≥-0.20, including its underside. Its slightly embedded base overlaps shoulder volume; it is not another court slab. Sparse foliage and irregular rock groups remain inside the bank footprint and below Y=2.20. No front closure, corner return, or route/fence-end modification is supplied.

## Runtime files

- `shoulder_render.glb`: gravel/soil render surface; `shoulder_collision.glb`: one closed SOIL support component, 192 triangles.
- `bank_render.glb`: bank soil, 13 rocks, sparse woody shrubs and fully opaque leaves. `bank_collision.glb`: closed bank SOIL component (136 triangles) and 13 individual closed rock components (80 triangles each).
- `five_placements_render.glb`: the same two modules placed at all five approved semantic instances for layout review. Use either this or the individually instantiated modules, never both.
- `placement_manifest.json`: exact transforms and semantic owner labels. **No engine numeric owner IDs are assigned.** Import must allocate after the existing 1044 objects according to engine rules.

## Collision and ownership handoff

Each placed shoulder must receive its own authoritative SOIL support owner; each bank its own separate authoritative SOIL support owner. Associate each bank's rock components with that bank's solid collision grouping or the engine's appropriate per-rock owners. Preserve distinct closed components; do not merge the 13 rocks and soil into a >240-triangle convex input. Every supplied component is independently below 240 triangles. Soil is a closed triangle surface, not a convex hull requirement: if the engine needs convex-only colliders, decompose the bank/shoulder into per-grid-cell closed prisms before hull import. Do not replace soil with a single hull that bridges the bank shape.

Use proxies only from the collision GLBs. Never infer collision from the render scene or opaque leaves. Woody shrub branches and leaf meshes are visual-only, with no invisible foliage blockers. Engine owns support, player movement, collision generation, owner allocation, destruction and native tests. Preserve existing owner 0 and accepted motel/sign/fence assets unchanged.

## Materials / provenance

All geometry, textures, branch shapes, leaves and scatter are authored here from deterministic numeric construction, seed 230808. No downloads, paid assets, external texture reuse, scans, or generative imagery. Five original tileable material sets contain base color, tangent normal and roughness at 512²: gravel/soil, bank soil, stone, bark and olive leaves. All render textures are embedded in GLBs and packed in the .blend. Roughness and normal inputs are non-color; base color is sRGB. Foliage is opaque, requiring no unverified alpha-cutout behavior. Materials use ordinary glTF metallic-roughness PBR, no metallic surfaces or transmission.

Restrained detail is intended for near-realism/Xbox 360-scale assets. The bank has exposed soil and separate irregular shrubs rather than a hedge wall. Repetition of the two modules is intentional; do not change approved instance bounds to hide it.

## Rebuild

Requires Blender 4.x/5.x, Python 3, NumPy and Pillow. From the kit folder:

    python source/make_textures.py
    blender -b --python source/build_kit.py
    python qa/independent_qa.py

`SKIP_RENDERS=1 blender -b --python source/build_kit.py` regenerates runtime GLBs and source .blend without previews. The script contains original geometry and material authoring, not engine code. The .blend remains an editable source snapshot with proxies hidden. Two separate builds are compared in `qa/determinism_report.json`; runtime byte equality is the reproducibility target, not Blender UI/file metadata.

## Evidence and limits

`preview/two_modules_neutral_reimport.png`, `preview/east_edge_offline_close.png` and `preview/all_five_offline_context.png` are rendered only after importing exported GLBs. The plain court patch is an **offline footprint reference**, excluded from runtime deliverables; no motel, sign or fence is reconstructed or modified. `qa/independent_report.json` contains independent binary GLB geometry checks and transform/seam assertions. Native engine collision, owner assignment, traversal, destruction and accepted motel/fence context remain engine-side acceptance work.

### Repeat seams and first-pass scope

This is deliberately a repeated two-module first pass, not a uniquely scattered perimeter. The same five-shrub/thirteen-rock grouping repeats at each bank instance. No extra variants were added. Top-surface UVs use a 2 m repeat, so an 8 m tile changes longitudinal UV by exactly four full repeats; adjacent shoulder and bank copies meet at equal repeat phase. Custom top normals derive from the analytic height surface and match across longitudinal tile ends. Side and bottom UVs use dominant-axis projection to avoid collapsed UV triangles. Original texture noise uses wrapped samples and normal derivatives use periodic neighbors. Read the QA report for the actual exported seam tolerances.

The extra `preview/player_height_edge_offline.png` is a 1.65 m eye-height perspective from the court-reference side across the east seam. All four previews remain offline, with no claim that owner/support physics has been tested in the game.

### Actual geometry and draw-group budget

Each bank render contains 12,256 triangles: 136 soil, 1,040 rock, 10,120 opaque leaf, 960 woody branch/trunk. These are consolidated into four identity-root render objects/primitives, one material each. A shoulder has 192 triangles and one primitive/material. All five placements total **62,240 rendered triangles, 25 objects/primitives, five unique materials** before any engine batching. This is a meaningful foliage cost, not a validated Xbox 360 performance budget; profile actual engine draw submission and pixel overdraw. No LOD system is claimed or supplied.

Each placed bank has 14 separate closed collision components (one soil and thirteen rocks); each shoulder has one. Full layout therefore has 75 closed collision components, 6,840 total collision triangles, and ten semantic SOIL support owners. Opaque leaves and wood contribute **zero collision triangles**. Render-only groups must never be used to combine the collider pieces.

The last quality pass removed all isolated dry-stick grass lines and replaced uniform ellipsoidal boulder profiles with bounded asymmetric original form deformation, without increasing proxy topology or foliage density. The unchanged two-module grouping remains visibly repeated.
