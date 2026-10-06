# Independent storefront kit QA

Status: PASS on the input SHA-256 values in independent_blender_report.json.

## Coverage and results

- 24 new modules and 15 packaged reused types fresh-imported individually in Blender.
- All 41 GLBs (39 individual files and both example scenes) inspected directly as binary glTF.
- Finite positions, normals, UVs, and supplied tangents; valid indices; zero geometric or UV-degenerate triangles; unit normals.
- All embedded image payloads decode to nonempty images. No external buffer/image URI dependencies.
- New-module bounds, dimensions, triangle counts, material counts, file byte lengths, IDs and SHA-256 values match the manifest.
- New glass is core glTF alpha BLEND with alpha 0.20; no transmission/refraction extension dependency.
- Every normal-mapped new/example primitive carries validated tangent XYZ/handedness. Tangents are optional and omitted for normal-free exported materials; packaged reused source bytes remain unchanged.
- Every rendered instance is tagged render-only with colliders disabled. No rigid bodies, animation, skins or presentation cameras appear in GLBs.
- All 151 assembly transforms match declared position, yaw, scale, and recorded world matrices. Imported quaternion-mode reuse sources produce XYZ-mode instances with the intended rotation.
- All 11 actual world-space door/transom socket pairs match exactly.
- All six supported props pass actual vertical support rays against supporting mesh triangles. Contact gaps are 2.0–3.5 mm.
- Native UVs match declared per-face metric projection exactly at all 36,484 mesh corners. Basecolor is sRGB, normal/roughness data is linear, and normal maps are tangent-space with unit strength.
- Floor slab top is −0.008 m and finish top is 0 m; no coincident exposed floor faces.

## Explicit route scope

The routes tested are both front doors through each shop's central aisle into shared rear service space; the rear cross-passage linking the shops; shared rear service to exterior exit; and the front sidewalk between entries.

A 0.30 m radius / 1.80 m height capsule is tested at no more than 0.05 m horizontal intervals. Clearance uses exact capsule-axis segment-to-triangle distances with conservative BVH candidate selection. Floor support uses actual downward triangle rays. Numerical clearance tolerance is 3 mm. Entry sills are an explicit ≤20 mm step allowance (actual sill height 18 mm), so a runtime controller would need appropriate step handling.

This verifies the listed static routes only. It is not a continuous swept-motion proof, whole-map navigation test, collider/navmesh generation, engine certification, or building-code/accessibility certification.

## Reproduce

Run from the kit directory:

    OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 python qa/independent_glb_audit.py
    OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 blender -b -t 2 --python-exit-code 1 --python qa/independent_blender_audit.py

The audits are read-only with respect to assets and save only their own QA reports/logs. The Blender report includes input hashes and checks they did not change during the audit.
