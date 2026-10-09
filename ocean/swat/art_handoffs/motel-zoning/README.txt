BRIAR COURT | PHASE A MATERIAL ZONING SOURCE HANDOFF

SOURCE ART ONLY. Current engine supports ordinary opaque glTF per primitive and does not yet blend two full ground materials. This handoff supplies a mask, explicit sampling contract, both unchanged scan sets and an offline custom-channel-blend illustration. Engine implementation, native lighting, normal handedness and native acceptance remain with the engine owner. No shader/engine code or new geometry is delivered. Outer-ridge phase remains on hold.

START HERE
runtime/zoning_mask.png — single-channel 512x512 R8 linear dirt weight.
runtime/mapping.json — exact mapping, all 54 support IDs, eligibility, exclusions, texture channels, scale and color spaces.
source/build_mask.py — original editable CC0 generator; source/composition_proposal.json defines measured polygon cores.
source/scans/ — both complete AVAILABLE source material sets, each diffuse/normal/roughness at 1K and 2K, original PNG bytes. No displacement/AO maps were used or downloaded. Engine target uses existing 1K set; 2K originals are archival alternatives, not extra simultaneous runtime loads.
source/SCAN_PROVENANCE.json — original source links, SHA-256, license.
reference/v3_opaque_ground_UNCHANGED.glb — byte-exact conventional opaque v3 reference; NO blend. Do not import alongside the existing ground. It is not an engine integration.
offline_source/ — editable packed Blender source and reproducible custom offline preview only.
review/ — mask diagnostic plus labelled before/after offline views.
qa/ — numeric, source-preservation and independent checks. Native testing was not run.

EXACT SAMPLING
Bounds: X[-64,64], Z[-48,52], metres, Y up. PNG row 0 is the -Z rear edge. Left-to-right is +X. For a top-left texture API use u=(X+64)/128, v=(Z+48)/100. Pixel centres are X=-64+(column+.5)*.25 and Z=-48+(row+.5)*.1953125. Bottom-left texture APIs must flip V: 1-(Z+48)/100. Do not infer image-row orientation from a shader language. Compare qa/control_points.json and review/mask_diagnostic_grid.png on import.
R/255 is dirt weight; gravel weight is 1-R. No alpha, sRGB transfer, opacity, normal information or other channels. Use CLAMP_TO_EDGE, bilinear LOD0, NO automatic mips for this exact mask. Return zero outside the world bounds before clamping. A full diagonal texel (.317249...m; exact value in mapping.json) zero collar protects excluded boundaries from bilinear leakage. Wider filtering or mipmaps require fresh exclusion-safe generation and QA; ordinary averaged mips can contaminate protected surfaces.

APPLY ONLY TO THE LISTED ELIGIBLE TOP SURFACES
12 flat soil ground_support primitives are eligible. The other 42 supports (40 apron/grade pieces plus road and driveway), existing court owner 0, original shoulder/bank assemblies, protected X[-3,3] Z[5,48], two fence-end clearance discs, all side/bottom faces are unchanged. All support ownership remains inherited i -> 1119+i, no IDs reassigned. Source manifest numeric_owner nulls are preserved. Mask zeros are not authorization to apply an unrelated material to excluded surfaces.

COMPOSITION
W/R/E/F cores preserve the approved polygon vertices and dirt weights .8/.85/.7/.75. Outside each core the weight falls linearly across six metres. Union uses max, followed by exclusions. F is the explicitly clipped far-verge exception: core Z48..52, with six-metre falloff clipped at Z46, plus one-Z-texel conservative filter guard. The sharp clipped strip onset is intentional to preserve the roadside gravel band; it is not described as a full six-metre transition. No procedural noise or stone-scale enlargement.

MATERIAL CONTRACT
Gravel repeat 8/3 m, dirt repeat 2m, phase origin world XZ=(0,0). Existing gravel mesh TEXCOORD_0 stays unchanged. Blender UV convention is X/repeat,Z/repeat; actual exported glTF flips V to 1-Z/repeat. For dirt from existing gravel glTF UV use dirtU=gravelU*4/3, dirtV=1+(gravelV-1)*4/3. Both samplers REPEAT. Do not normalize per support or alter phase at seams.
Decode diffuse PNG as sRGB to linear then multiply gravel RGB by .65 and dirt RGB by 1 before blending. Alpha stays 1. Roughness PNG is linear scalar, factor1; blend scalar channels. Metallic0. Decode OpenGL tangent-space normal maps as non-color, normalScale=.65 for BOTH sources, normalize; interpolate in the SAME tangent basis and renormalize. Reuse the verified existing ground tangent basis and test orientation natively; do not treat mask UVs as normal-map UVs. Base-color factor is a linear material factor, not a PNG edit. No map recoloring, painted lighting, AO, height or displacement.

REPRODUCTION AND PACKAGES
Unpack source-full and runtime into the same root for relative texture references. Run python source/build_mask.py with NumPy/Pillow/matplotlib installed; this regenerates only the mask/metadata/diagnostic, never baseline or geometry. The offline Blender builder uses the frozen v3 reference inputs, explained in its source. Runtime-mask package is incremental mask+metadata; both material sets are companion source inputs to be deduplicated with the existing asset library. Runtime integration must not load duplicate scans solely because they appear here.

ACCEPTANCE STILL REQUIRED
Engine owner should check control points/UV orientation, same-camera aerial and 1.65m-eye appearance, channel blend and normal orientation, exact protected zones including minified views, original texture scale, no duplicate scan residency, and unchanged 54-support traversal/ownership. An offline image is not proof that the current renderer can display this result. No new collision or landscape support contract is implied.
