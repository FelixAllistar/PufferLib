# Independent static-art audit: BC_SHELTER01

**Result: pass within the stated offline static-art scope, with the caveats below.** No blocking visual or technical finding remains in the reviewed candidate. This is not native-engine or construction acceptance.

## Exact identity

- Runtime SHA-256: `79dd4edacb9af1b47d98294e2a75b0c109a2d469d1dd9fdf74af72ee63aeeb3d`
- Packed editable-source SHA-256: `105e41a64dd82b84f78eadab1189f9e1c9b477d595f09ff82fc61229a5f4fd36`
- Order-independent oriented surface SHA-256: `cb14b611139f6ac1e25d27a0dd7bc3cc5ebceb2061f65481d635cbdbee888571`

## Verified

- Actual GLB: 3.220 × 1.689 × 2.379 m (width × depth × height), 7,868 triangles, 15,984 exported vertices, 3 mesh nodes/material primitives, 3 opaque single-sided PBR materials, 5 embedded images, 8 descriptive anchors and one root. No external texture URI, rig, animation, lights or cameras.
- All 160 component solids are closed, consistently oriented and positive-volume. Zero degenerate triangles, duplicate triangles, winding-opposed normal corners or collapsed UV triangles were found by the supplied checks. Position, normal and UV values are finite.
- The isolated packed source reopened with no maps directory. Its supplied helper re-export exactly matches world-space oriented triangles, normals, UVs, material assignments, decoded texture pixels, effective samplers and PBR factors. A relocated recipe with copied input maps does too. Byte identity is not required; the `source_parts` inventory order is nonsemantic.
- All five packed images match the source-map hashes. The four new seeded texture maps regenerate byte-for-byte. The inherited steel map remains an explicit input.
- Numerical rays verify all 12 seat/bearer contacts; frame, backrest, bracket and screen-clip joins remain connected-looking. All four baseplates reach Z=0. The gutter has a real drain opening and the downpipe has a hollow outlet. These are visual/contact checks only.
- Eight fresh actual-GLB builder views and three independent diagnostic views were inspected and hash-bound to this exact runtime. Front/rear silhouettes, pale timber, muted weathered metal, feet, seat supports, roof edges and gutter details read coherently. Previously visible black contact patches were corrected. The final independent same-facing coplanar triangle scan reports zero pairs at its stated precision.

## Useful caveats

- No native engine import, collision, navigation, cover, occupancy, destruction, target performance or draw-call acceptance was tested.
- UV0 contains intentional atlas reuse and repeating steel UVs outside 0–1; no secondary lightmap UV set or LODs are supplied.
- The asset is an assembly of overlapping solids, not one boolean-unioned solid. Contact checks and drainage appearance do not establish engineering, installation, weatherproofing or accessibility compliance.
- No full Khronos glTF validator or exhaustive self-intersection proof was run. The stated offline checks and sampled visual views are the scope of this result.

## Evidence and reproduction

`AUDIT_REPORT.json` contains the decision and exact identities. `geometry_checks.json`, `reproduction_checks.json`, `contact_checks.json` and `visual_review.json` contain measured results. Independent diagnostic images are in `diagnostics/`, with their own SHA binding.

For a fresh rerun, copy the scripts in `reproduce/` into a new empty directory, keeping their filenames together. With Blender 4.3.2, Python, NumPy and Pillow available, run:

```sh
python rerun_checks.py /absolute/path/to/candidate RUNTIME_SHA256_FROM_ABOVE
blender -b -t 3 --python render_diagnostics.py
```

The runner writes isolated snapshots, exports, logs and comparisons beside itself. It does not edit the candidate. The visual conclusion requires inspection of the newly rendered images; an automated success is not a substitute for that review. Full temporary rebuild trees are intentionally excluded from this evidence package.
