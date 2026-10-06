# Rifle 7 visual cleanup R2

Private authoring candidate for engine review. Not installed or user-accepted. Original art redistribution rights remain unestablished; do not publish.

## Changes
- Derived 4096-square base-color and packed ORM maps retain source texture detail while compressing bright wear contrast. Dark-polymer grip/stock-shell and rubber butt-pad use isolated original connected components. The rear sight has a restrained dark, rough coating. Original normal image bytes are unchanged; normal strength is 0.65.
- Rear ring outer contour is refined from 8 to 32 angular divisions with smooth wall shading. All original vertices remain in the payload unchanged. Only 64 ring triangles are replaced by 1,024 local triangles; the octagonal inner aperture remains exactly the source opening. Cosmetic outer-profile movement is at most 0.442354 mm, within exactly unchanged overall bounds.
- Body has 11,233 triangles (was 10,273), magazine 780. Two static meshes, one primitive/material each, magazine mesh index 1, no skins or animation.

## Protected measurements
All stock/muzzle/sight centers, source unit/frame, grip and support-hand surfaces are unchanged. Stock-to-muzzle vector length remains 0.901056695457287 m. Engine axes are +X forward/+Y up/+Z right. No pose or grip changes. GEOMETRY_VALIDATION.json records direct GLB byte/array checks. 10,209 non-ring body triangles and every magazine attribute/index remain exact.

## Files
- rifle7_visual_cleanup_r2.glb: complete main rigid runtime candidate
- rifle7_material_only_r2.glb: identical material cleanup with original geometry/topology intact
- rifle7_visual_cleanup_r2.editable.blend: packed candidate and matched review setup
- rifle7_finish_recipe.editable.blend: packed original/derived images and editable bake recipe
- rifle7_original_reference.blend: packed exact GLB import for review; original GLB remains frozen separately
- comparison_neutral.jpg, comparison_rear_detail.jpg, comparison_offline_ads_120mm.jpg: original left, candidate right, same camera/lights/exposure
- views/: full-size individual renders
- rifle_bindings.json: original measured bindings, only asset name/hash and candidate note changed
- MATERIAL_RECIPE.json: exact treatment and selected original component ranges
- tools/: reproducible source scripts (resolve source_frozen/original_package relative to authoring root)

## Review camera and limitations
Offline ADS camera uses the measured sight direction and 120 mm rear-sight eye relief, perspective 55 mm, 36 mm sensor, 1200 x 700. Blender coordinates: camera (0.17982166017, -0.000000208445, 0.12475645878), target (0.65497362614, -0.000000146431, 0.12472589687). This is an offline matched optical comparison, not the live engine camera. Neutral and detail images use identical Cycles lighting, AgX and -1.5 exposure for both versions.

The functional aperture is deliberately still octagonal. The stem/rail and other low-poly geometry remain unchanged. Some original edge wear remains, especially on rails and trigger guard; this is a restrained cleanup, not a retexture/redesign. Runtime shader response, mipmapping, animation and actual ADS visibility still need engine validation. Material-only fallback is available if the local ring refinement is undesirable.

## Frozen source
Original GLB SHA-256: daac57ca6bf40c9157808f88a1d1184c11f19ea4bada63cc54ca260b24392b48
Original recovered ZIP SHA-256: aabbd0fd0318b87ebcae27e106290cc2ef2dd7341c4c5c45a8c91674a7272407
Original raw ZIP parts remain in the authorized private Drive folder 1jpTjRpjolpPelVH_Fd4EnKXchfhLApTX. Both parts were read-only restored and the complete archive/GLB verified before authoring. Exact original PNGs and archive are under the separate source_frozen checkpoint. The initial material pass was retained separately under candidate_r1 and is not the delivery candidate.
