# Guest-room accessories v1

Two original unbranded decorative props, intended for modest Xbox 360-era environmental fidelity. These are candidate art exports, not integrated engine assets or final user art approval.

## Runtime contract
- Metres. glTF Y-up; original Blender Z-up. Origin is centered on tabletop contact plane, z=0 in Blender / y=0 in glTF. Place on a support surface without extra offsets except the tray's inner floor/mat when nesting the bucket.
- Each GLB has one asset root. Parts have part_owner and part_id extras; transforms belong to that root. Keep the bucket's lid/knob with the bucket and tray's inset mat with the tray. Do not promote parts to independent physics objects.
- Explicit LOD0/LOD1 GLBs; LOD1 is a separate alternative, not auto-switching or engine registered. Choose distance thresholds only after engine-camera evaluation.
- Bucket uses vinyl_brown and satin_steel material instances; tray uses tray_charcoal and vinyl_brown. Separate per-object AO atlas on UV1. Material textures repeat on UV0; UV0 may intentionally exceed 0..1. UV1 is within 0..1. 512px maps, no external image URIs.
- Basecolor sRGB. Tangent OpenGL normal map, linear. Roughmetal PNG linear: R unused white, G roughness, B metallic. AO separate linear UV1. Metal and roughness factors are 1 so maps control them.
- Fixed closed bucket only: no opening, water, ice simulation, interaction, UI, collision, navigation, damage or fracture implementation. Tray apertures are visible real geometry, not a collision promise.
- Proposed render-only support ownership: inherit authoritative desk/nightstand; remove with support if required. COLLISION_PROPOSAL.json is advisory only. No engine assignments made.

## Rebuild
Requires Blender 4.3+ (tested installed build recorded in manifest), Python 3, NumPy and Pillow.
1. python make_textures.py
2. blender -b -t 4 --python build_props.py -- --out /absolute/output/path
3. python validate_glb.py /absolute/output/path
4. blender -b -t 4 --python render_exports.py

The editable .blend retains separate named parts and original material nodes. build_props.py is authoritative editable parametric source. Exports and texture hashes are tested against a clean rebuild; .blend serialization itself is not asserted byte-identical. Preview scripts import final runtime GLBs. desk_context.png contains an original illustrative desk/wall, not Room 101 or an engine screenshot. Preview set dressing is not exported.

## Required engine AO routing
All materials: baseColorTexture → TEXCOORD_0, metallicRoughnessTexture → TEXCOORD_0, normalTexture → TEXCOORD_0 (scale 1). occlusionTexture → TEXCOORD_1 (strength 1). Basecolor has no AO or illumination baked in. The exported metadata is checked in MATERIAL_CHANNELS.json and validate_glb.py. A renderer supporting only UV0 must add second-UV sampling for AO; it must not silently sample the atlas on UV0. This package preserves the correct separate-channel contract.
