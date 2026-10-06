# Batch 16 QA summary

- **24 new modular assets**, 17,804 triangles across the new masters.
- **15 existing CC0 source designs**, preserved byte-for-byte, with original IDs and SHA-256 provenance. They are not counted as new designs.
- **151 declared assembly instances**, plus eight scene-only site meshes. Complete example: 159 meshes / 82,858 triangles. Roof-off example: 122 meshes / 72,254 triangles.
- All **41 GLBs** pass the independent binary audit, including finite attributes, valid indexed triangles, decoded embedded texture images, bounds, counts, material metadata and hashes.
- **39 fresh per-piece Blender imports**, both complete examples, packed native textures and metre units pass.
- **619 native/import checks** cover exact matrices, quaternion-mode reuse conversion, 11 socket connections, 36,484 metric UV corners and six triangle-supported props. The final presentation save is rechecked before packaging.
- Normal-mapped primitives carry valid explicit tangents. 20,716 tangent vectors checked; maximum unit-length error 0.0000612, normal-dot error 0.0000708, handedness exactly ±1. Optional unused tangents on un-normal-mapped materials are omitted.
- Five specified routes pass a **0.30 m radius / 1.80 m height capsule** test with actual triangle floor support. Routes: each shop entry to shared rear area, rear cross-passage, rear exit, front sidewalk between entries. Capsule-axis segment-to-triangle distance is exact at 5 cm route samples. Entry sills are 18 mm high and use an explicit maximum 20 mm step allowance. This is not continuous-motion, engine, navmesh or building-code certification.
- An isolated reconstruction using only packaged inputs reproduces **all 24 module GLBs and both example GLBs exactly by SHA-256**. Native Blender container bytes and previews are not claimed to be byte-deterministic.
- Five inspectable location renders include four player-height views and a bounds-framed cutaway. All 24 new pieces also have individual previews and a labelled contact sheet.

Machine evidence is under `qa/`. The package script rejects stale reports by comparing their audited hashes to current files before archiving. Each archive is read back in full, with unique safe paths, ZIP CRC and SHA-256 validation. Adjacent `.sha256` files identify the archive bytes; `SHA256SUMS` inside the archive verifies the included payload.

## Limits

No game loader, runtime, physics, collision, navigation, damage, networking or interaction changes. No prior asset/batch was edited. Render-only art and offline geometry checks only. Interior render sampling grain remains visible because this Blender build has no OpenImageDenoise support; no generated image or retouching replaces the actual geometry renders. Third-party CC0 texture provenance is retained, and the earlier provisional image-generated plaster is excluded.
