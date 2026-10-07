# Room101 v4 independent read-only audit

## Decision
No blocking defect found in the tested art-asset contract. This is an asset-level technical pass, not target-engine or visual approval.

Tested eight exported GLBs (27,648 triangles; 28,182,476 bytes), the saved v4 .blend, the sealed v3 .blend and all 90 v3 SHA256SUMS entries. Used independently written raw GLB/accessor/PNG and Blender-state parsers; no candidate QA script was imported or used as proof. All writes stayed in this diagnostics directory. No source, asset, engine, post or upload changes were made.

## Final artifact authority
This refreshed report applies to the current final GLB hashes in SUMMARY.json and GLB_AUDIT.json. Bedframe SHA-256 is `70d1aec713051fad89c25dffba07f4e1e5e95a993e5d76e1a15e834365f0c006`, superseding its earlier ordering-dependent hash. Source .blend and build input hashes are recorded in SUMMARY.json.

## Verified
- Actual final GLB bytes independently match all eight corresponding portable_v4_test outputs. The build driver, four pass modules, pinned v3 source, and all supplied texture files also match. The candidate portable QA report hashes agree with the actual files. This audit inspected and verified the existing independent rebuild; it did not launch a third rebuild.
- All 90 sealed v3 file hashes match. V4's pinned .blend is byte-identical to sealed v3. Every v4 GLB matches EXPORT_MANIFEST hashes and stayed unchanged during this audit.
- Eight GLBs each have one correctly named, identity-transform ownership root; all 38 nodes are reachable with no cycles/orphans. All descendant source_owner tags resolve to their intended root. All carry render_only=true and collider_enabled=false. No cameras, animations, skins, lights or glTF extensions are exported.
- No existing source object transform or parent changed. Changes are confined to the intended eight owner subtrees. The five new and three removed objects are the documented bedding replacement pieces. No existing materials or inspected camera/light/render/world/color settings changed. Existing non-version custom properties are unchanged.
- All aggregate v4 bounds remain inside the corresponding v3 envelopes. Export/source bounds agree exactly for seven assets and within 0.000000477 m for the mattress. Bedframe, floor, partition, wall and window bounds are identical; basin differs only by approximately 4.7 nm at the bottom. Mattress contracts by at most approximately 18 μm. Sheet-stack intentionally contracts.
- No non-finite attribute/matrix values, out-of-range indices, degenerate geometry triangles, missing sampled UVs, or zero-area textured UV triangles. All 51 primitives are TRIANGLES. Normal-mapped primitives have finite tangents and valid +/-1 handedness. No substantial normal/tangent length or orthogonality error.
- All 22 embedded image references are PNG and at most 1024 pixels on either axis. Linen is 1022×1024, plaster/carpet 1024×1024, retained legacy maps 192×192. No external image dependencies.
- All 25 comparable embedded texture/channel uses exactly match the source texture pixels. Roughness is correctly packed in G; carpet AO in R; unused metallic map channels are white but dielectric factors are explicitly zero. Chrome, aluminum and exposed steel use effective metallic=1 via the glTF default when omitted. No scalar material is incorrectly reported as missing required maps.
- Curtains: each retains its exact source positions, topology and main-surface UVs. Exactly 112 previously zero-area rim polygons per curtain get new UVs and flat shading. Both exported curtain triangle position signatures match v3. All window material JSON and embedded image bytes are unchanged. V3's 224 degenerate UV triangles per curtain become zero in v4.
- Main folded blanket is one closed, position-welded connected manifold shell; all three sheet leaves and both curtains are also single closed manifold components. The separately authored hem is one open strip with 68 boundary edges and no >2-use edges.

## Precise caveats
- No target-engine test or renderer/collision/AI/damage validation; GLB extras are metadata only.
- Existing isolated portable rebuild independently verified byte-for-byte for all eight outputs and input files; this audit did not launch another rebuild.
- No same-light image/art-quality acceptance in this audit; parent handles the pending renders.
- Sheet-stack render bounds shrink and its lowest visible point is 10.1 mm above unchanged datum; engine bounds must not be derived from replacement render AABB.
- Blanket main shell is closed; separate turned hem is intentionally an open strip, not collision geometry.
- Window hooks and rail have different exported cap triangulation despite unchanged source topology and identical exported position sets; full GLB index-buffer equality is not claimed.
- Scalar porcelain/enamel/chrome/steel/tile are authored factors, not texture-backed scans; retained MC15 exterior/slab finishes remain base-color-only with roughness factors.
- Tangent precision residual max 0.0001404 (approximately 0.008 degrees from exact orthogonality) is recorded as non-blocking numeric rounding, not hidden.

The sheet-stack local visible AABB changes from ±0.237421 × ±0.173822 m and Z 0–0.150427 m to ±0.235 × ±0.169 m and Z 0.0101–0.074499 m. The unchanged owner pivot is intentional; support contact must be judged in the composed render. Do not describe its render AABB as identical.

The non-blocking tangent residual peaks at |N·T|=0.0001403987. Length errors remain below 0.0001. The audit logs a warning above 0.0001 and an error above 0.001; these are declared audit tolerances, not a claim of Khronos validator certification.

Window hook/rail source meshes are fully unchanged, but exported n-gon cap triangulation differs between v3/v4. All their unique transformed vertex positions agree at 1 μm and their topology remains closed; do not promise byte-identical or index-identical window exports.

## Actual material factors and bindings
- basin_pedestal_028_v4.glb / R101V4_glazed_porcelain: metallic 0.000, roughness 0.220; scalar, no maps
- basin_pedestal_028_v4.glb / R101V4_polished_chrome: metallic 1.000, roughness 0.180; scalar, no maps
- basin_pedestal_028_v4.glb / R101V4_drain_interior: metallic 0.000, roughness 0.650; scalar, no maps
- bathroom_partition_4m_019_v4.glb / R101V4_bathroom_fine_plaster: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear; normal scale 0.350
- bathroom_partition_4m_019_v4.glb / R101V4_bathroom_wall_ceramic: metallic 0.000, roughness 0.320; scalar, no maps
- bathroom_partition_4m_019_v4.glb / R101V4_bathroom_cream_trim: metallic 0.000, roughness 0.360; scalar, no maps
- bathroom_partition_4m_019_v4.glb / R101_SCAN_fine_plaster: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear; normal scale 0.350
- bedframe_single_institutional_021_v4.glb / R101V4_bedframe_warm_cream_enamel: metallic 0.000, roughness 0.400; scalar, no maps
- bedframe_single_institutional_021_v4.glb / R101V4_bedframe_caster_rubber: metallic 0.000, roughness 0.760; scalar, no maps
- bedframe_single_institutional_021_v4.glb / R101V4_bedframe_exposed_steel: metallic 1.000, roughness 0.420; scalar, no maps
- bedframe_single_institutional_021_v4.glb / R101V4_bedframe_dark_rail_enamel: metallic 0.000, roughness 0.460; scalar, no maps
- bedframe_single_institutional_021_v4.glb / R101V4_bedframe_attachment_enamel: metallic 0.000, roughness 0.440; scalar, no maps
- folded_bedsheet_stack_027_v4.glb / R101V3_bed_linen: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear; normal scale 0.160
- mattress_dirty_022_v4.glb / R101V3_bed_linen: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear; normal scale 0.160
- mattress_dirty_022_v4.glb / R101V3_folded_blanket: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear; normal scale 0.240
- room_floor_4x6m_008_v4.glb / MC15_concrete: metallic 0.000, roughness 0.940; baseColor_sRGB
- room_floor_4x6m_008_v4.glb / R101V3_carpet: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear, occlusion_R_linear; normal scale 0.180; AO strength 0.300
- room_floor_4x6m_008_v4.glb / R101V4_matte_bath_tile: metallic 0.000, roughness 0.480; scalar, no maps
- room_floor_4x6m_008_v4.glb / R101V4_bath_grout: metallic 0.000, roughness 0.880; scalar, no maps
- room_window_insert_016_v4.glb / R101V3_curtain_linen: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear; normal scale 0.200
- room_window_insert_016_v4.glb / R101V3_satin_aluminum: metallic 1.000, roughness 0.370; scalar, no maps
- room_window_insert_016_v4.glb / R101V3_black_gasket: metallic 0.000, roughness 0.800; scalar, no maps
- room_window_insert_016_v4.glb / R101V3_opaque_glazing_compatibility: metallic 0.000, roughness 0.170; scalar, no maps
- room_window_insert_016_v4.glb / R101_cream_enamel: metallic 0.000, roughness 0.360; scalar, no maps
- solid_wall_4m_013_v4.glb / MC15_stucco: metallic 0.000, roughness 0.960; baseColor_sRGB
- solid_wall_4m_013_v4.glb / MC15_concrete_dark: metallic 0.000, roughness 0.970; baseColor_sRGB
- solid_wall_4m_013_v4.glb / R101V4_bathroom_fine_plaster: metallic 0.000, roughness 1.000; baseColor_sRGB, roughness_G_metallic_B_linear, normal_linear; normal scale 0.350

## Evidence files
- SUMMARY.json: concise decision, current source/build/GLB hash authority and caveats
- PORTABLE_REBUILD_INDEPENDENT_CHECK.json: independent actual-file hash and byte comparisons against the portable rebuild, matching inputs, and build-log provenance
- GLB_AUDIT.json: every GLB hash, node hierarchy, triangle statistics, material factors, image statistics and accessor/UV/tangent measurements
- SOURCE_COMPARISON.json and SOURCE_SNAPSHOTS.json: independently read source state, object/material changes and render settings
- BOUND_COMPARISON.json: actual source/GLB aggregate AABBs and unchanged TRS
- DETAIL_CHECKS.json: exact source/packed-channel pixel comparisons, connected components, window positional evidence
- CURTAIN_UV_COMPARISON.json: exact rim-only UV changes and retained main UVs
- audit_glb.py, audit_source.py, audit_details.py, audit_curtain_uv.py: reproducible read-only checks

Not covered: target-engine import, draw batching/performance, material-enum or gameplay binding behavior, collision/nav/AI/damage, self-intersection or support-contact certification, visual realism, or repeatability across different Blender versions/platforms. Those require their respective owners' checks.
