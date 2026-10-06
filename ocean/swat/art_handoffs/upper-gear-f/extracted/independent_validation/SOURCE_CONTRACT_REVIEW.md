# Independent F source contract audit

Current binaries were opened read-only in Blender 4.3.2. All hashes were checked before and after. Final checkpoint 1e586e869db06e5e6a0026b5c56860e5e0ed7f2d9bc1ae9bbee0eb8ca1fd3e35 (18,171,720 bytes) is independently verified. Binary/runtime checks remain separate.

## Verified source identity

- Native 70-bone definitions, order, parents, complete rest matrices, heads, tails, lengths, deform flags, inherit-scale modes and connection flags are exact. Static and movement scenes have the same 72-bone rig.
- Gear_Elbow_L and Gear_Elbow_R are additional deform bones parented to mixamorig:LeftArm and mixamorig:RightArm. They are unconnected, length approximately 45 mm, and inherit_scale NONE. No live rig constraints or object drivers were found.
- Retained native source IDs preserve exact bind positions, all weight values, polygon topology, UV coordinates, material definitions and material assignments. Native neutral 70-bone TRS channels are exact in the new neutral action.
- Rifle, both magazines and spare-magazine sleeve preserve every audited mesh, UV, normal, material, weight, transform, modifier and visibility field.
- Static and movement source meshes are exactly equal for all audited fields, including full material node definitions and packed image hashes.

## Geometry changes

- Native body: 24,850 vertices / 25,026 polygons / 46,301 triangles.
- Retained SWAT_Wearer: 22,645 vertices / 22,882 polygons / 42,339 triangles.
- Removed: 2,205 vertices / 2,144 polygons / 3,962 triangles across the 27 source components recorded in SOURCE_CONTRACT_AUDIT.json.
- Added: SWAT_ElbowCap_L/R, each 386 vertices / 768 triangle polygons; SWAT_Headset_L/R, each 320 vertices / 290 polygons / 636 triangles.
- Caps each use exactly weight 1 on their corresponding Gear_Elbow bone. Headsets each use exactly weight 1 on mixamorig:Head.
- Retained custom split normals are near-exact rather than bit-identical: maximum vector error 0.0006052295, maximum angular error 0.0346771 degrees.

## Materials and UV

- Retained body keeps map1 and native Ch15_body.001 / Ch15_body1.001 material slots. Four new objects have UVMap and two slots: SWAT_Gear_Matte_Polymer / SWAT_Gear_Soft_Padding.
- Each cap assigns 352 polygons to polymer and 416 to padding. Each headset assigns 164 polygons to polymer and 126 to padding.
- New gear node base colors are (0.09, 0.10, 0.11, 1) and (0.038, 0.044, 0.048, 1). The Principled node roughness is 0.78; the unrelated material datablock roughness is 0.4. Use the node values.
- Sleeve has no UV layer, preserving source behavior. Native packed image and shader identity were checked in the read-only snapshots.

## Export hazards

1. Use a separately versioned 72-bone asset contract. Neither old vertex IDs nor old skin-slot indices are valid by assumption; resolve names and exported node-to-skin-slot mappings.
2. Preserve all positive skin influences. SWAT_Wearer has 416 vertices above four influences: 340 with five, 74 with six and two with seven. Source weight sums deviate from one by up to 0.001751788; normalized all-influence skinning is the correct numerical comparison to Blender. Do not truncate.
3. Export evaluated world transforms into exported hierarchy-local TRS. The new carrier inherit_scale NONE mode is not a portable glTF bone property. Check TRS decomposition residuals and actual FK/skin results.
4. Explicitly select the nine character mesh objects plus the rig. All nine meshes, including both magazines, are visible in the source. Exclude the review floor, cameras and lights.
5. Include neutral static reference and the bounded three-second carry articulation only. Source also contains ADS Intent / Anatomical Gear F, which is not approved for this package.
6. Movement is 181 LINEAR keys per scalar channel over source frames 0..90 at 30fps (half-frame/60Hz sampling): 720 scalar TRS curves, 130,320 scalar keys, 20 carrier curves. Keep duration exactly three seconds and preserve both endpoints.
7. Preserve custom split normals, per-corner UV seams, material assignments and all original oriented triangles. Vertex splitting in glTF can legitimately change vertex counts; compare by semantic bind data, not array length alone.
8. The scoped source seal hashes mesh fields and material names, but not full material node values, visibility or modifier state. This independent report additionally checks those fields.

## Qualification boundaries

Natural neutral hold and bounded 4-degree raise/return proof only. No ADS, eye/camera, shared shoulder support, gameplay or full fastening/force approval. The approximately 6.9 mm left padding gap is in the stored ADS static pose. During this bounded movement the central left-padding maximum is approximately 2.64 mm; cupped rim separation remains larger.

## Evidence

Machine-readable detail: SOURCE_CONTRACT_AUDIT.json. Full native and F rest definitions: SOURCE_BONE_RESTS.json. Reproducible reader: audit_source_contract.py. No source files were saved or modified.
