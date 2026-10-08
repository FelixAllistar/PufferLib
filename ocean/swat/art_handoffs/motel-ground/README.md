# Briar Court connected road / ground v1

Separate, bounded connected-ground/road blockout, not a finished roadside environment. Finite outer edges remain visible, with no far landscape context or road markings. This connects the approved local support envelope; it does not eliminate the world boundary or imply an infinite environment. No engine code, scene asset replacement, numeric owner allocation, upload, or native traversal certification is included.

## Placement contract

- World metres, +Y up, +Z frontage. Runtime vertices are world-placed; import both GLBs at identity, once.
- All visible new ground within X [-64,64], Z [-48,52] has matching closed support. No flat ground or decorative background continues beyond this envelope. The exposed finite boundary is not a hidden wall or certified play limit.
- Subtract the exact existing court X/Z [-24,24], east shoulders X [24,30], Z [-12,12], and west shoulders X [-30,-24], Z [-8,8]. 12,800 m² envelope = 2,544 m² preserved footprint + 10,256 m² new support.
- 54 new semantic support owners (40 exact graded wedges plus 14 merged flat rectangles), each 12 triangles and closed. Engine assigns numeric IDs. Collision and render positions are identical; collision GLB has no materials. Register the collision GLB once; do not collide render geometry as an additional floor.
- Asphalt road X [-56,56], Z [36,44], exactly 8 m wide. Flush asphalt driveway X [-3,3], Z [24,36], exactly 6 m wide. No markings, curbs, lane dividers, fences, invisible barriers or extra obstacles.
- Far verge X [-56,56], Z [44,48]. Original apron boundary X ±40, Z ±36 is retained as logical region metadata; remainder of the approved outer envelope is equally real support rather than noncolliding flat background.
- Flat Y = -0.08. Existing shoulder top edge at outer X ±30 is Y = -0.20. New ground grades over 2 m to flat elevation, including longitudinal end profiles. Profile samples come from the existing 1 m shoulder mesh stations, not only its analytic idealization. No vertical step between new support and existing shoulder support. Solid bottoms Y = -0.60.
- Court owner 0 and its source/material/physics remain untouched. Five accepted roots, all 75 existing components and existing bank-to-shoulder overlap are unchanged. The bank's pre-existing raised lip and steep end face remain visible; they are not new floor seam errors and have not been buried or edited.

Full exact piece bounds, top corner heights, identity transforms, regions, material scales, preserved transforms, and protected reservations are in runtime/placement_manifest.json. There are no invented numeric owner IDs.

## Clear reservations

Staging (0,-0.079,8), extraction (0,-0.079,10) radius 2.5 m, protected X [-3,3] Z [5,13], and 6 m clear corridor X [-3,3] Z [13,48]. Fence ends near (12.5,0) and (12.5,6) retain 2 m clearance from any new above-ground object. Existing court reservations contain no new geometry. The corridor beyond the court consists only of flush road/soil support.

## Materials

- Reuse Poly Haven Clean Asphalt, Dimitrios Savva, CC0; original approved 1K diffuse/OpenGL normal/roughness PNGs, all 16-bit, 2.1 m repeat. Approval source ZIP SHA-256: 1bef0bbc83fece8a04f5a690586b37020b477490558a8fd9dc86d3526f083960.
- Reuse existing licensed Poly Haven gravel_ground_01 maps, CC0, 8/3 m repeat matching the accepted shoulder material scale. No new download, recolor, baked shadow, displacement, AO, or texture paint.
- Base color is sRGB; normal and roughness are linear. Asphalt normal strength 1.0, gravel strength 0.65. Runtime upward-face UV direction is U +X, V -Z. The asphalt roughness image is kept as the original grayscale16 PNG rather than Blender's 8-bit packed derivative: glTF samples G as roughness, metallicFactor=0, so B contributes no metallic response.
- Actual engine texture decode, mip/anisotropy and exposure still require native review. The approved custom engine has separately reported PNG16-to-GPU channel/row/mip validation for these exact maps; this candidate does not repeat that native validation. Strict glTF 2.0 requires 8-bit sRGB baseColorTexture, so deliberately preserving the approved 16-bit base color is a custom-engine portability exception, not a generic standards-conformance claim. No unapproved 8-bit derivative is substituted.

## Files and rebuild

- runtime/connected_ground_render.glb: matching closed visual solids, embedded textures, semantic names.
- runtime/connected_ground_collision.glb: matching closed collision solids, no material dependencies.
- runtime/placement_manifest.json: full support/placement matrix.
- source/connected_ground.blend: editable source with packed textures.
- source/build_ground.py and preserve_roughness.py: deterministic original authoring and lossless export fix.
- source/textures/: exact input maps. provenance/: exact prior manifest, approved plan and texture provenance.
- qa/: independent geometry and texture checks, repeat-build hashes, render and plan scripts.
- preview/: runtime GLB reimported context overview, player eye at 1.65 m, east/west module joins, topdown and annotated placement diagram.

Run with Blender 4.3.2 from any directory:

    blender -b --python source/build_ground.py
    python qa/independent_checks.py

The second command uses only Python standard library. Two complete Blender builds matched the runtime files byte for byte (qa/first_build_sha256.txt and second_build_sha256.txt). The .blend is an editable source and need not be byte-stable; runtime output is the determinism target. The source .blend uses Blender material nodes; rerun the supplied build/export path to preserve original 16-bit asphalt roughness.

Preview reproduction requires the separately preserved motel_surroundings_kit_v2materials/runtime/five_placements_render.glb at its sibling path; it is not redundantly redistributed here. qa/render_previews.py reimports both runtime sources and constructs intentionally simplified court/motel/fence/sign reference blockouts for scale. Those blockouts are preview-only, excluded from exports, and are not proposed replacements or native-engine screenshots. Preview Cycles is 24 samples, no denoising, AgX exposure +1; slight render grain is expected.

## Native acceptance still required

This is local geometric/material QA only. Engine import must assign owners and verify support registration, full traversal including finite edges, both fence-end paths, extraction and 6 m corridor, spawn/AI routes, rifle behavior and fallback behavior. Do not claim safe walking outside the finite support boundary. No engine edit or approval is implied by offline previews.
