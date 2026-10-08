# Verified local results

All 26 independent checks passed. Full reproducible detail: qa/independent_checks.py and qa/independent_checks.json.

- 54 closed, manifold, outward-wound render solids and 54 matching collision solids.
- 12 triangles per support (648 total), comfortably below the 240-triangle limit.
- All 1,431 pairs have zero positive-area footprint overlap. Exact envelope-minus-existing-cuts coverage is 10,256 m², with no uncovered flat-ground region or geometry beyond the approved bounds.
- New support has one connected footprint. All 108 new seams are height-continuous. 70 old-shoulder samples agree within 1 micrometre, with west transforms applied from the actual manifest.
- 40 graded wedges are exactly unchanged by flat compaction. 266 flat grid cells became 14 rectangles: 12 soil, one road, one driveway. Two material sets and 54 current render primitives; this is not a claim of two draw calls or global minimum arbitrary-polygon partition.
- No new above-ground blocker in the protected staging/extraction/corridor/fence-end reservations. New top elevations never exceed Y -0.08.
- All three referenced asphalt PNGs are byte-identical original 1024² 16-bit sources. Grayscale roughness supplies G; metallicFactor 0 guarantees no metallic contribution. Tangents were rebuilt from the emitted geometry/UVs and verified against U +X, V -Z, +Y up, including handedness and orthogonality.
- The accepted kit's six runtime hashes and 43 source-baseline hashes remain unchanged. Original bank-to-shoulder overlap is untouched. The candidate does not rewrite owner 0 or accepted motel/fence/sign assets.
- Two full final rebuilds match current runtime SHA-256 exactly.

Known portability exception: the custom-engine-approved 16-bit base color is intentionally retained. Strict glTF 2.0 expects 8-bit sRGB baseColorTexture. This is not generic glTF conformance. Per the engine report, these exact maps already passed independent native PNG16-to-GPU channel/row/mip validation; this offline asset task did not repeat native decoding checks.

Preview images are labeled offline blockout context. Motel/sign/fence/court are schematic stand-ins, excluded from the runtime. Accepted perimeter modules are reimported unchanged. The finite boundary and absence of far landscape context/road markings are deliberate limits. Native traversal, AI, rifle, fallback and engine support registration remain untested by this deliverable.
