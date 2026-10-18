# BC_SEDAN01 independent QA

Status: PASS for the tested offline static-asset scope, with the three appearance caveats below. All nine actual-import preview images were hash-verified and visually inspected. This does not claim target-engine or gameplay acceptance.

## Audited release identity

- Runtime: `BC_SEDAN01_parked.glb`, SHA-256 `1c50b6c4c31570fc441e4eef47f567b872848ac82895fd308cba9c393ab7a298`
- Packed source: `BC_SEDAN01_editable.blend`, SHA-256 `689c4b8a5a65f8dcd7c80e03834f699abc8e51d743e251b5bed340e33b776420`
- Recipe: `build_sedan.py`, SHA-256 `55c54f079568f7971364d50557934de623dc5b8ab232a9ebe9fc3cd3df1ac055`
- Blender used for independent checks: 4.3.2. Numeric binary checks use independent Python/NumPy code; no builder QA functions were imported.

## Runtime binary findings

- glTF 2.0 binary header and declared length valid; 1,012,828 bytes.
- 13,124 triangles; 12,713 exported vertices; 6 mesh nodes plus 1 root; 12 material primitives; 4 materials; 5 embedded 512 × 512 RGB PNGs.
- A 13k-triangle candidate was accepted for this task. The earlier 8–12k suggestion is not a hard pass criterion.
- All positions, normals, UVs and transforms finite. All index references in range.
- No zero-area geometric triangles. No zero-area UV triangles. UV coordinates are in [0,1]. Overlapping UVs are intentional for shared tiling/atlas materials; this is not a unique lightmap unwrap.
- Normal lengths are within about 1.2e-7 of one. Zero corner normals oppose their geometric face normals at the audit threshold.
- Spatially welding all positions within each logical mesh at 1 micrometre yields zero boundary edges, nonmanifold edges, inconsistently wound edge pairs, or duplicate geometric triangles.
- Four PBR materials all use OPAQUE mode, alpha 1 where specified, and single-sided rendering. Glass is intentionally opaque and backed by cabin geometry. No transmission extension or alpha blending is present.
- No external buffer/image URIs, animations, skins, cameras, lights, or required extensions. Render stage/camera/lights are not in the runtime asset.
- Six mesh nodes carry identity transforms, beneath an identity root. Dimensions in glTF axes are X 1.960 m, Y 1.454 m, Z 4.362 m. Maximum width includes mirrors.
- Root origin is at the ground-plane midpoint of the wheel contact pairs; it is not the exact front/back bounding-box midpoint. Blender uses -Y front/+Z up; glTF uses +Z front/+Y up. Wheel nodes share the root origin and are not animation-ready axle pivots.
- Four materials do not imply four draw calls: there are 12 material primitives. Actual engine batching, draw counts and performance were not measured.

Detailed evidence: `glb_audit.json`, `runtime_topology.json`.

## Editable source and reproducibility

- All 151 editable source mesh objects are closed/manifold, finite, and have positive signed volume, including every connected component.
- Four inward-wound weatherseal solids were found during the preliminary audit and corrected before this frozen release. The frozen source passes the same independent test.
- All five image datablocks are packed with correct 512 × 512 dimensions and valid pixel data.
- The exact original packed .blend was copied into a separate directory containing no maps. On reopen, all five relative external image paths were confirmed absent; packed images still loaded. No source-path rewriting was necessary for this test.
- An independently implemented grouping/export pass from that isolated source produced the same geometry, per-corner normals/UVs, material definitions and embedded image bytes, ignoring harmless vertex/mesh ordering differences. Triangle comparison retains winding and quantizes attributes to 1e-6.
- The supplied `export_packed.py` run on that isolated source produced a byte-identical GLB.
- A clean recipe run in a separate output directory, starting from copied Python sources with fresh procedural map generation, produced a byte-identical GLB.
- Each of the five fresh procedural PNGs is byte-identical to the corresponding source/embedded map. Inspection of the recipe found seeded NumPy noise, simple colors, grooves and grids, with no network access or source imagery. This supports reproducible procedural provenance; it is not a legal originality clearance.

Detailed evidence: `source_audit.json`, `packed_reopen_report.json`, `packed_reexport_comparison.json`, `official_packed_comparison.json`, `clean_rebuild_comparison.json`, `texture_provenance.json`, and the accompanying logs.

## Wheels and assembly contacts

- Independently measured Blender wheel centers: front left (0.765, -1.280, 0.315), front right (-0.765, -1.280, 0.315), rear left (0.765, 1.280, 0.315), rear right (-0.765, 1.280, 0.315) m, within float precision. These match the root metadata.
- All four tire radii are 0.315 m and widths 0.200 m. All four reach exactly Blender Z=0 (glTF Y=0), each along two tread vertices; none extends below ground.
- Independent Blender BVHTree queries report no triangle-overlap pairs between any tire and the coachwork shell or its corresponding arch lip.
- Bidirectional all-vertex-to-triangle clearance samples are at least 45.32 mm to the shell and 38.96 mm to an arch lip. These are sampled distances, not an exact global triangle-to-triangle minimum.
- Several closed exterior assembly parts intentionally overlap at their attachment seams. This is suitable static-exterior assembly topology, not a Boolean-unioned manufacturing solid, dynamic wheel envelope, collision proxy, or destruction/support graph.

Detailed evidence: `source_audit.json`, `wheel_clearance.json`.

## Final actual-pixel review

All nine images in `render_binding_verified.json` match their recorded SHA-256 and the frozen runtime identity. Actual pixels were inspected in front, rear, front/rear three-quarter, side, eye-level, underside, wheel-contact and top views. Pillars and opaque glazing remain sealed; four-door proportions, lamp/bumper assemblies and parked wheel stance are legible. The underside is intentionally simple and closed. The corrected top image uses a 6.7 m orthographic width and includes both bumper tips and the full overhead silhouette. Its new image hash was checked, actual pixels were inspected, and all other eight view entries remain unchanged. Detailed per-view observations are in `visual_review.json`.

## Visual limitations accepted for this candidate

1. Rear-deck/boot shutline is faint and intermittent in some oblique lighting/views because the small strip does not remain uniformly exposed along its entire length. Thin seam details are not equally legible at every angle.
2. Minor pinched/faceted highlights are visible near the bottom valance in frontal lighting. Numeric winding and normals pass, but the low-poly surface is not uniformly smooth.

3. Wheel-cover vent slots are uneven and partly occluded by the dish in the wheel-contact close-up. These are not uniformly clean vent openings.

These are appearance limitations. No topology, wheel-contact, opaque-glazing or rebuild blocker was found in the frozen asset. It is an anonymous static background sedan, not a hero-quality close-up vehicle.

## Not tested / not supplied

- Import into the actual game engine, target shader implementation, runtime visual parity, render performance, memory use or batching.
- Physics, collision, navigation blockage, gameplay cover behavior or world placement.
- Driving, steering, suspension motion, damage, door articulation, interiors, animated pivots or mechanical accuracy.
- LOD transitions, mip/texture streaming behavior, lightmaps, baked lighting or large-instance stress tests.
- Formal Khronos glTF Validator execution. The tests here are independent structural/geometry checks and Blender export/reimport checks, not a claim of every validator rule.

## Reproduction

Audit scripts are supplied alongside this report in the independent audit directory. Given the final package's `source/` and `runtime/` directories:

1. Run `audit_glb.py runtime/BC_SEDAN01_parked.glb glb_audit.json` and `audit_weld.py runtime/BC_SEDAN01_parked.glb runtime_topology.json` with Python, NumPy and Pillow installed.
2. Run Blender 4.3.2 in background mode on the packed source with `audit_source.py -- OUTPUT_DIRECTORY` and `audit_clearance.py -- wheel_clearance.json`.
3. Copy the packed .blend alone into an otherwise empty folder and run `reopen_export.py -- OUTPUT_DIRECTORY`; it asserts every external map path is absent before exporting.
4. Run supplied `source/export_packed.py -- --output packed.glb` on the isolated packed source; compare SHA-256 with the runtime.
5. Set `SEDAN_OUT` to a clean output directory, execute `source/make_textures.py` with Python, then `source/build_sedan.py` with Blender. Compare the resulting runtime SHA-256 and use `compare_glb.py` for semantic diagnostics if needed.

The detailed reports distinguish completed tests from untested engine integration. Earlier pre-freeze data must not be substituted for this release's hash-bound results.
