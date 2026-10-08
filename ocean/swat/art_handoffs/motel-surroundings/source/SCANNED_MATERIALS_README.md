# Briar Court v2materials: scanned-material candidate

A separate, provisional material-only candidate for the accepted v1 surroundings geometry. Do not replace the baseline archives. No native engine acceptance, frame-time, GPU-memory or collision performance claim is made here.

## What changed

Three material sets now use free CC0 photographic surface scans from Poly Haven. Runtime maps are 1024 × 1024; unchanged original 2048 × 2048 maps are retained separately for optional future use. Original downloaded PNG bytes, provider MD5, local SHA256, asset metadata and URLs are preserved in `source/scans/` and `source/SCAN_PROVENANCE.json`.

- Gravel shoulder: [Gravel Ground 01](https://polyhaven.com/a/gravel_ground_01), Rob Tuytel. Published source calibration: 3.0 × 3.0 m. Deliberate runtime repeat: **8/3 m = 2.6666667 m**, not the source's native physical scale. Aggregate linear scale is 88.8889% of the calibrated source. This modest 11.1111% reduction gives exactly three repeats per unchanged 8 m module and preserves repeat phase at all east and west joins.
- Soil bank: [Dirt](https://polyhaven.com/a/dirt), Charlotte Baglioni. Source calibration and runtime planar repeat: **2.0 × 2.0 m**, four repeats per module. Planar projection stretches along the sloped bank surface; dimensions refer to projected axes, not geodesic surface distance.
- Rock surface: [Rock Boulder Dry](https://polyhaven.com/a/rock_boulder_dry), photography Dimitrios Savva, processing Rico Cilliers. Source calibration approximately **1.8 × 1.8 m**, preserved as runtime planar repeat. The original dominant-face projection is retained with a uniform UV scale change, rather than remeshing or unwrapping geometry.

[Poly Haven asset license](https://polyhaven.com/license): CC0, including commercial use and redistribution. The website's page text and promotional/example renders are not included as CC0 assets. [Texture standards](https://docs.polyhaven.com/en/technical-standards/textures) describe photo-based acquisition and measured dimensions. Provenance describes photographic scanned materials; no claim is made that each selected older asset used a particular acquisition instrument.

Diffuse is assigned to base color in sRGB. OpenGL tangent normals and pure roughness maps use Non-Color. No AO, roughness-with-AO, ARM shadow channel, displacement, image-generated detail or painted scene shadow was added. Rendered shadows come from the offline lights and existing geometry.

For these three materials, native glTF `normalTexture.scale` is 0.65 (serialized 0.6499999762), versus baseline 0.45. Native `roughnessFactor` remains 1.0 (implicit default) multiplied by the scan's roughness map; metallicFactor is 0, baseColorFactor is [1,1,1,1]. No custom shader extension is needed. Material alpha remains OPAQUE and existing double-sided status is retained. Bark and leaves, including their 512² maps, normal strength, geometry, UVs and bindings, are byte-exact to the baseline.

## Preserved contracts

All exported POSITION, NORMAL and triangle-index accessors, node definitions and world transforms match v1 exactly. Both collision GLBs and placement_manifest.json are byte-identical. No positions, shapes, bounds, footsteps/collision semantics, foliage, scatter, semantic instances or render batch counts changed. Collision stays at 10 separate SOIL components and 65 separate rock components across five placements, with zero foliage collision. Numeric engine owners are still assigned by the engine. All five placed modules remain 25 render primitives and 62,240 triangles.

Shoulder visual UVs are multiplied by 0.75. Stone visual UVs are multiplied by (1/1.8)/1.5 = 0.37037037. Bank soil, foliage and proxy UVs are unchanged. glTF export flips V; the audit accounts for this as V_new = 1 - scale × (1 - V_old). Source geometry creation is unchanged from v1. Candidate source and scripts are wholly separate from v1.

## Visual assessment and limits

Matched baseline/candidate player-height and module-close renders show substantially more recognizable gravel, dirt and weathered stone detail. Gravel remains fine aggregate, rather than oversized pebbles, at the documented 2⅔ m runtime repeat. Both sides retain continuous texture phase through the tile boundaries. The original color/material boundary between gravel and exposed bank remains hard, with no added terrain blend.

This is a surface improvement only. Rock silhouettes remain rounded/faceted; some existing dominant-projection transitions are more apparent under the richer stone texture. Repeated three/four texture motifs and repeated rock/shrub placement remain visible from elevated views. Unchanged foliage is still visibly low-poly. Do not describe the complete scene as photorealistic or claim that textures repaired those geometry limitations.

`preview/` contains four exactly matched camera/lighting pairs: player_height, module_close, west_seam and east_top_repeat. These are Cycles renders after GLB reimport, not engine screenshots. `qa/matched_cameras.json` records the shared camera/lighting settings. The dark court plane is an offline footprint reference only and is not exported.

## Validation

- `qa/preservation_report.json`: byte-exact baseline file tree, collision GLBs, manifest, geometry accessors, node transforms and foliage; only documented UVs/materials changed.
- `qa/independent_report.json`: zero failures and zero warnings; binary GLB geometry/topology/bounds/material checks and separate Blender reimport. All six module-to-module soil joins have zero UV repeat-phase error. Maximum normal mismatch is unchanged at about 0.005728°, below the existing 0.01° tolerance.
- `qa/texture_report.json`: all 18 downloaded source maps match provider MD5 and recorded SHA256, with dimensions and unmodified runtime input copies; native glTF material factors and raw edge-difference diagnostics.
- `qa/determinism_report.json`: all six runtime files rebuild byte-for-byte in two consecutive Blender builds.

These offline checks do not substitute for engine reimport, material interpretation, render-memory accounting, player/squad traversal, destruction/reset or performance tests. Existing v1 engine results do not automatically certify this candidate's native appearance.

## Rebuild and use

Blender 4.3.2 was used. Python QA uses NumPy and Pillow. No texture painting, resampling or generated material images are required. Source texture files are byte-exact copies of provider 1K downloads; Blender packs images in the editable source and exports native glTF textures.

From the candidate folder:

```
SKIP_RENDERS=1 blender -b --python source/build_kit.py
python qa/preservation_audit.py
python qa/texture_audit.py
python qa/independent_qa.py
blender -b --python qa/render_comparison.py
```

The preservation and matched comparison checks expect the untouched sibling `motel_surroundings_kit_v1`. Do not run v1's procedural texture generator against this candidate. `source/download_scans.py` is a provenance/retrieval helper, not required to rebuild from the supplied files.

Use either shoulder_render.glb + bank_render.glb with placement_manifest.json, or five_placements_render.glb for complete layout review, never both. The combined file duplicates module texture bytes for convenience. Every standalone runtime GLB is below 32 MiB, but compressed file size is not a GPU texture-memory measurement. No engine texture compression or mip policy is specified here.
