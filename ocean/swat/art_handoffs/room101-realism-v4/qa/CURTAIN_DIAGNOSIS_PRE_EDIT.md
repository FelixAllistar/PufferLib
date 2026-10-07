# Room 101 v3 curtain forensic inspection

Read-only inspection, 7 October 2026. No v3 files were changed; no v4 art was created. Blender was opened for inspection only, with no save or export. All new scripts and reports are in this sibling directory.

## Finding

The large dirty ladder/stripe pattern is very likely produced by the engine's room-shadow path, rather than by a corrupt linen map or a discontinuity in the main curtain surface. The strongest evidence is its upper boundary: it agrees with the downward room-shadow frustum edge predicted from the available earlier engine source to within a few pixels. The exact current renderer source and a current engine A/B capture are still needed to establish whether the immediate mechanism is depth bias, receiver-plane gradient, atlas sampling/coverage, or another shadow-path issue.

Do not compensate by painting brighter fabric, baking a counter-shadow, changing glazing transparency, changing collision ownership, or deforming the cloth around the artifact.

## Evidence from the actual engine image

Input: `../engine_v3_review_oct7/engine/room101-v3-window.png`, 960 x 800.

- The top of both panels is clean. Dirty shading begins abruptly at about y=263–267 on the right panel and y=278–282 on the left, following the same world-height boundary despite perspective.
- The older `../engine_handoff_oct7/lighting.c` uses a downward perspective room shadow camera with a 150-degree field of view, returning full visibility outside the shadow projection. Its fallback light origin for this room is (-6, 2.62, -3). At the curtain's mean depth, Z=-0.026, the forward frustum boundary is world height 1.8231 m: 2.62 - 2.974/tan(75 degrees).
- Projected through the exact current engine camera from its README (eye -6.35,1.55,-2.6; target -5.45,1.45,0; vertical FOV 45.781 degrees), that boundary passes through (705.43,262.24), (572.48,266.88), (242.17,278.39), and (146.86,281.71). These positions match the visible onset remarkably closely.
- Current v3 integration notes explicitly say downward room projections remain, with 768-pixel room tiles. The earlier C file is not proof of current numerical parameters; it is a testable prediction.
- Vertical per-pixel luma variation grows 6.75x in the selected left-panel lower region and 4.15x in the right-panel lower region compared with their clean upper regions. The lower-region mean darkens by approximately 19–20 code values. These are descriptive screenshot measurements, not radiometric calibration.
- The existing Cycles source render `candidate_v3_interior_window.png` does not show the ladder pattern. Its lighting/tone mapping differs, so it does not prove engine parity or rule out geometry interacting with a shadow map.

## Geometry and normal inspection

Each exported curtain has:

- 1,394 vertices, 2,784 triangles, one material and one UV set
- 40 subdivisions across 0.36 m, 16 subdivisions over 1.163 m, five sinusoidal pleats
- Pleat amplitude 13 mm, wavelength 72 mm, 1 mm solidified shell
- No degenerate 3D triangles; normal lengths 0.99999988–1.00000012
- No negative/nonuniform source transforms, no custom normals, and no remaining modifiers
- Continuously varying interior vertex normals: adjacent vertical row changes are at most 1.107 degrees, with mean approximately 0.398 degrees. There is no abrupt interior normal seam at the artifact onset
- Minimum original-pane-to-curtain separation of approximately 7 mm. The glass ends at source-local Y=.024; the nearest curtain surface begins at .031. This does not support coplanar pane/cloth z-fighting

There are two real but secondary source weaknesses:

1. All 1,392 source quads, including the solidified rim, are smooth shaded. Top/bottom interior-to-rim normal changes reach 53.8 degrees and affect the first/last approximately 72.7 mm strip. This can make the hem look rolled or inflated; it does not explain the common mid-panel shadow onset.
2. All 112 rim quads have collapsed UV area, generating 224 UV-degenerate rim triangles (8.05% of all triangles). All 2,560 main front/back triangles have nondegenerate UVs. The first draft of the GLB inspection's geometric-normal heuristic labels 64 side-rim triangles as 'major faces'; the Blender polygon-index inspection establishes that every UV-degenerate polygon is a rim, indices 1280–1391. These are not interior UV failures.

Tangents are absent in the GLB. That is not, by itself, a defect: the earlier engine source explicitly selects a signed UV-derivative basis for these materials, including a zero-determinant guard. Blender's own Mikk tangent calculation returns finite, unit-length tangents for every loop. If the current engine retains this derivative route, exporting tangents alone will not change its shading. A missing/currently broken derivative path still needs the engine A/B to exclude completely.

The exceptionally regular five pleats, nearly constant amplitude, and only 1.5 mm bottom sag also help explain the rigid/tubular design read. This is separate from the large dark artifacts.

## Material/map inspection

Embedded GLB maps match the supplied runtime texture identities. The source high-resolution diffuse, normal and roughness files match the hashes recorded in MATERIAL_PREPARATION.json.

- Linen texture dimensions are 1022 x 1024; metric repeats are .299415 x .3 m
- The basecolor is sRGB. Normal and roughness source nodes are Non-Color. The exported normal scale is .200000003, metallic factor is 0, and no occlusionTexture is bound to the curtain
- The embedded roughness map is correctly packed in G; R/B are white, with metallic multiplied by zero. Roughness mean .8700, range .7412–.9490
- Basecolor RGB means are 175.19,172.41,163.70 in 8-bit sRGB. It contains uniform small weave detail, not a lower-only macro pattern
- Normal-map decoded vector lengths range .994835–1.005108, consistent with quantization. At scale .2, the effective normal perturbation is mean 1.493 degrees, 99th percentile 3.323 degrees, maximum 5.070 degrees. This is mild microrelief, not a large discolored stripe
- UVs continuously repeat approximately 1.20 tiles across and 3.88 tiles vertically. There is no material split or UV discontinuity through the main faces at the upper edge of the engine blotching

## Minimal discriminating engine test, before an art change

Use the current window camera, same lights/exposure, current opaque pane and original collision authority. Capture three diagnostic variants:

1. Current baseline
2. Same material and normal scale .2, but bypass only room-shadow visibility sampling on the curtain receiver. Preserve sun light/shadow and geometry
3. Same original shadow path, but set curtain normal scale to zero

Interpretation:

- Pattern disappears in 2 but remains in 3: room-shadow path confirmed. Inspect projector boundary handling and receiver-plane depth bias/slope or atlas edge sampling; do not change cloth maps
- Pattern disappears in 3: inspect signed derivative basis, map orientation/decoding and whether perturbed normals incorrectly drive shadow bias
- Pattern remains in both: capture unlit basecolor and geometric normals next, then inspect duplicate draws, face culling and transform consistency

Also visualize the room-shadow UV/depth and out-of-frustum mask. The predicted cutoff line should coincide with the image's clean/dirty boundary. Current renderer parameters can confirm or falsify the old-source 150-degree model immediately. Toggling only curtain casting is a secondary test for thin-shell self-shadowing, not a proposed production removal of shadows.

## Minimal source cleanup if a v4 curtain pass is approved

Keep the frozen v3 asset unchanged. In a separate candidate, give the thin rim nondegenerate UVs and split its shading normals or build a small controlled hem instead of smoothing across a 1 mm side wall. Preserve continuous smooth normals on the cloth faces. If the renderer will consume them, export and validate Mikk tangents. This addresses genuine edge-quality and tangent-robustness issues, but should not be sold as a fix for the shadow bands without the engine test.

## Scratch artifacts

- `gltf_inspection.json`: exported attributes, geometry/map statistics, material/sampler records
- `blend_inspection.json`: source normals, row deltas, UV degeneracy, material node/color-space inspection, pane gap
- `projection_and_image_stats.json`: reproducible frustum projection and selected screenshot measurements
- `inspect_gltf.py`, `inspect_blend.py`, `check_projection.py`: read-only inputs, sibling-directory outputs

Verified window GLB SHA-256: `7ce9e66a972b20c0d58e35053708ee5384e9a8e9284537424f16467f82422d09`, matching the sealed export manifest.
