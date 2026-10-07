# Room 101 v3 • integration contract

## Scope and baseline

This is an incremental candidate on the v2 art reported integrated locally on branch 5.0 at a406c933b. That revision and the current binding source/captures were supplied in room101-engine-bindings-and-captures-20261007.zip. The remote branch did not contain those SWAT changes when inspected; no remote-publication claim is made here.

Use RUNTIME_INSTANCE_BINDINGS.json for exact source assembly indices, engine tag.index values, existing asset indices, original placements/scales and replacement filenames. V3 targets only ten existing Room 101 instances. Inward-face wall variants must not replace the global shared end-wall asset.

Keep v2 facade __012, main door __015, walkway __010 and sconce __020. Replace v2 facade_optional_detail_overlay and door_optional_detail_overlay with the v3 versions. For plaque __018, use the v3 full model and remove the old v2 plaque_optional_detail_overlay; the new model includes painted numbers and its two mounting screws. Otherwise the old extruded numerals will duplicate the printed text.

Prefer an atomic v3 asset-set readiness check and full v2 fallback over partial combinations. This document does not edit the engine's binding arrays or assume the old five-instance loader already covers the new files.

## Geometry, transforms and ownership

Each core GLB root retains the original local datum. Apply the existing authoritative scale, yaw and origin once. Model bounds are not an instruction to normalize, resize or recenter. The mattress keeps the source nonuniform scale; new cloth UVs account for that inherited scale.

Blender is metres, Z-up, front -Y. Engine/glTF is metres, Y-up, front +Z: (x,y,z) -> (x,z,-y). CAMERA_MATCH.json and DETAIL_CAMERAS.json already provide the converted eye/target/up and exact effective FOV.

The window export has a frame root, named opaque left/right pane nodes and separately named curtain/detail nodes. ALL currently belong to the same original window object. Alpha remains 1, transmission 0, alphaMode OPAQUE. Do not introduce transparent glazing or split collision/AI visibility/destruction ownership during this art integration. Those require the separate engine-controlled change.

Every child follows its source owner in main, shadow, cutaway and secondary cameras, and disappears with parent removal. The ceiling remains part of its existing roof owner's cutaway behavior. No new independent damage ID, collider, bullet stop, acoustics or navigation obstacle is supplied. Keep the original compiled collision arrays and logical materials.

All ten native base meshes retain original vertex positions and instance TRS. Some cosmetic faces are removed and new owned render geometry is added; unused vertices can be omitted by GLB export. Complete candidate core-model bounds remain inside original envelopes within 2 micrometres of floating-point tolerance. This is an art-envelope check, not a claim that visual topology equals original collision.

The main door mesh, hinge, root placement and dimensions are unchanged. Important distinction from the older art prose: the supplied engine motel.c uses a 50mm authoritative door thickness (half.x=0.025m), while the source visible slab is 45mm. Preserve BOTH as they are; do not rebuild the engine slab from art bounds.

## Trim correction only

TRIM_REVISIONS.json and qa/TRIM_GEOMETRY_VALIDATION.json record the measured changes. Eight coplanar bead-corner patches are removed by inward butt-joint trimming; minimum joint gap is about 1mm. Casing fronts stay at source-local y=-0.1375m; backs now meet wall y=-0.090m. Side-to-head gap is about 0.5mm. Caulk is seated against the wall.

The main facade/door/walkway meshes and transforms are unchanged relative to v2. No new exposed same-facing coplanarity was found. Four new buried underside/plinth contacts at floor z=0 are documented. The two inherited original jamb/head overlaps remain unchanged, and the threshold still has its prior +0.4mm visual offset. Do not interpret this as a blanket fix for renderer edge/shadow aliasing.

## Material channels

- Basecolor textures: sRGB; glTF baseColorFactor is linear. Keep the engine's current factor conversion policy; do not compensate the art again with scene-light tint
- Normal textures: RGB linear OpenGL/+Y. Apply glTF normalTexture.scale once. New strengths: curtain .20, bed linen .16, blanket .24, carpet .18, painted number 1.0. Reused plaster stays .35
- Raw roughness PNGs: linear R. Embedded glTF metallicRoughnessTexture: linear G roughness / B metalness, with glTF factors
- Dielectric surfaces have metallicFactor 0; satin aluminum has metallicFactor 1 (which can be omitted as the glTF default). Plain enamel, metal, gasket and compatibility-glass materials intentionally use scalar factors where no texture is required
- New scanned/textured fabric, carpet and typography materials include actual basecolor, normal and metallic/roughness images. There are no unsupported procedural material graphs to recreate
- Candidate UVs already encode physical repetition. Do not apply the documented repeat size again. Use repeat sampling with mipmaps; use the signed UV derivative basis already used by the supplied engine path
- All embedded images are at most 1024px. Some old context material maps are smaller. High-resolution source maps are not runtime inputs
- No AO multiplication is baked into basecolor. Carpet016 AO is exported in ORM R with occlusionTexture.strength=0.30; raw R8 AO remains separately available. Pixel checks prove packed R matches that raw AO and packed G matches raw roughness exactly. Other materials have no occlusionTexture and use white/unbound AO. Engine screen-space contact AO remains optional and is off in the supplied default HDR captures

MATERIAL_BINDINGS.json lists new material contracts; source/V2_MATERIAL_BINDINGS.json covers reused ones. qa/EXPORT_VALIDATION.json lists the actual per-file embedded maps/factors, not just intended authoring nodes.

## Acceptance checks still needed

Verify hierarchy-aware loading of every new multi-node GLB, restored scalar/default PBR factors and normal scales, opaque panes, inherited mattress scale, camera parity, 0/45/100-degree door and parent-removal behavior, unchanged compiled collision/world state, roof cutaway and location unload/reload. Check per-model texture duplication/residency and target GPU timing. Original rooms must remain unchanged outside the specified instances. Review the updated doorway and both interior cameras under the engine's real lighting before adoption.

## Updated default-engine reference

CURRENT_ENGINE_BASELINE.json identifies the actual 7 October Windows GTX1060 v2 captures and supplied ZIP hash. The default HDR source is Poly Haven's CC0 Kloofendal 48d Partly Cloudy (Pure Sky), https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky (Greg Zaal original, Jarod Guest sky edits; https://polyhaven.com/license). The HDRI itself is not bundled or recreated in Blender here. Engine exposure is 1.1; source Blender uses its prior +0.7EV AgX setup, a different scale.

The new exact-camera source comparison uses engine entrance eye (-6.7,1.64,4.2), target (-6.95,1.26,0), vertical FOV45; interior eye (-7.2,1.62,-2.1), target (-6.45,1.22,-5.2), vertical FOV64. Both are engine Y-up with up(0,1,0), 960x800. Entrance door is closed as specified. Engine interior door pose was not specified; the source interior pair uses the preserved authored100-degree pose. Labels explicitly distinguish camera matching from lighting/renderer matching.
