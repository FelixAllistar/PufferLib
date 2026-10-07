# Room 101 v4 runtime handoff

Status: separate art candidate over the engine-integrated v3 baseline. This package has not been tested in the native engine. Keep v3 installed/pinned until all eight v4 replacements and their validation pass. It adds no locations and no new authoritative world objects.

## Exact replacement scope

Use RUNTIME_INSTANCE_BINDINGS.json for hashes, original assembly recipes, asset IDs and engine tags. The source assembly indices are 008 (floor), 013 (rear wall), 016 (window), 019 (bathroom partition), 021 (bedframe), 022 (mattress/bedding), 027 (folded linen) and 028 (sink). The existing engine tag is assembly index + 1. Only those Room101 instances change. Do not replace the underlying shared asset-bank design globally.

These eight files are a delta over complete v3. For the five instances already overridden by v3, remove the corresponding v3 render mesh and all its prior decorative children before drawing the v4 replacement. Bedframe, sink and rear wall supersede their original Room101 render meshes. Do not layer both versions. All other v3/v2 overrides remain, including AC, painted number plaque, facade/door overlays, main facade/door, walkway, sconce, side walls and ceiling. The old physical plaque numerals remain omitted exactly as in v3.

Load all eight files as one candidate set and retain a complete v3 fallback if any file is missing or invalid. No renderer implementation is included or requested.

## Placement and ownership

- GLBs use metres, Y-up, original identity-local datum. Apply the existing world instance transform exactly once. Preserve node-local transforms and all child geometry.
- Mattress retains source scale (0.68, 0.98, 1), which becomes engine scale (0.68, 1, 0.98). Its UVs account for that physical scale. Do not apply scale a second time, recenter a model, or regenerate normals in the wrong space.
- Keep original compiled collision, object bounds/IDs, damage and material gameplay enums, AI visibility, navigation, placement recipe and pivot. These are rendering replacements, not a request to compile new geometry into simulation.
- All aggregate exported geometry lies within the original owner envelope. Some cloth is visibly thinner/softer within that envelope. The folded-sheet stack is intentionally lower than its old 150 mm block; original simplified collision remains authoritative, so do not infer an exact new cloth collider from the art.
- Every child belongs to its existing owner. Owner destruction/removal hides all children and shadow contribution. No decorative child gets a world ID, collider or independent damage lifetime.
- No door, hinge, frame opening, threshold or door overlay is revised in v4. Keep the tested v3 0/45/100-degree door and removal behavior. The unchanged 45 mm art door slab and 50 mm authoritative door are still distinct.
- Window panes stay opaque, alpha 1, no transmission. Pane/frame/curtain nodes remain separated for inspection but retain the original single window authority/collider. No see-through/AI-visibility claim is made.

## Material binding

MATERIAL_BINDINGS.json records the exact exported values for each occurrence. Use glTF material semantics rather than a material-name heuristic.

- Decode basecolor images as sRGB, then multiply by linear baseColorFactor. Preserve alpha 1.
- Decode OpenGL (+Y) normal maps as linear data and preserve normalTexture.scale. Unchanged values: curtain .20, bed linen .16, folded blanket .24, plaster .35, carpet .18.
- Metallic-roughness images are linear: G roughness, B metallic; multiply by roughnessFactor and metallicFactor. White B with metallic factor zero is dielectric, not metal.
- New ceramic, enamel, chrome, steel and rubber use scalar glTF factors where appropriate. They are authored appearance estimates rather than scanned/calibrated materials. Do not replace absent maps with a universal roughness or metalness.
- Carpet AO remains separate linear R at strength .30, sharing the packed image with roughness G/metal B where exported. No new material AO and no baked basecolor shadow were added. Other materials have no occlusion binding.
- All embedded images are at most 1024 pixels on an axis. Linen stays 1022×1024, preserving its original aspect. Higher-resolution source maps are in the source archive.
- Optional finite glTF tangents are included. Continuing the already validated signed UV-derivative normal basis is acceptable. A tangent-path renderer change is not requested by this art handoff.

## Curtain diagnosis and test

The v3 engine image's broad lower curtain ladder pattern is strongly consistent with room-shadow coverage/self-shadow behavior. Using the supplied earlier 150-degree downward projector, its frustum boundary projects to approximately y=263–267 pixels on the right panel and 278–282 on the left, matching the observed onset. Current renderer source and controlled captures are still required to isolate the exact cause.

V4 changes only the confirmed secondary source defect: thin-rim UVs and split shading normals. Both panel position/topology hashes, main-face UVs, maps, opacity and material strengths are unchanged. Each panel has 112→0 zero-area rim UV quads; main-face top/bottom normal discontinuity falls from about 53.8 to 1.16 degrees. This is not claimed to fix the broad engine shadow bands.

At the supplied window camera, compare: (1) baseline, (2) only curtain room-shadow receiving bypassed, normal map unchanged, (3) original shadow path with curtain normal scale zero. Also inspect projector coverage/UV/depth. Do not bake a counter-shadow, remove the opaque pane, or change authority to hide the artifact. The engine owner owns this test and any renderer changes.

## Matched-camera review and native checks

CAMERA_MATCH.json supplies exact engine eye/target/FOV at 960×800 for entrance, bathroom, bed and window. Source before/after pairs use identical pinned Blender lights, tone mapping, seed and settings. They are not native HDR engine parity. Both source interior variants use door 100 degrees; the engine bathroom comparison's exact existing test door pose was not specified. Bed/window match the engine's stated 100-degree inspection pose. NEUTRAL_CAPTURE_SETUP.json describes the separate open-light bedding inspection, with architecture hidden only for that diagnostic.

The original native v3 captures/README are retained under qa/engine_v3_reference. Their performance report is one busy-workstation combined art+shadow run, 28.054→30.623 ms mean, not an art-only measurement or a speedup. V4 has no native timing result.

Before accepting v4, the engine owner should rerun: eight replacement bounds; normal/roughness/metal/AO factors; parent removal and shadow invalidation; opaque visibility and unchanged world bytes; roof cutaway; retained door poses/removal; resource lifecycle/fallback; and matched native v3/v4 entrance, bathroom, bed and window captures. Test both close edges and ordinary gameplay distance. Record GPU/CPU frame and memory cost separately if measured.

Remaining context is intentionally bounded: desk, folding chair, toilet and other bathroom fixtures remain old stylized assets. Bedframe retains its institutional design. Curtains remain regular short pleats. The bedding is authored geometry, not cloth simulation. This is a realism increment, not final art acceptance or equivalence to a commercial reference game.
