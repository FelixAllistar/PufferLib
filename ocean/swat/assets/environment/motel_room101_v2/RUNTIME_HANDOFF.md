# Runtime handoff • Room 101 v2 • candidate only

## Replacement scope

Use only on the listed Room 101 instances. Do not silently replace every shared motel design. The current source library, sunder's unfinished global surface revision, existing compiled collision and gameplay IDs remain pinned.

- room_facade_4m_candidate.glb -> INSTANCE__room_facade_4m__012
- room_door_leaf_candidate.glb -> INSTANCE__room_door_leaf__015
- gallery_walkway_4m_candidate.glb -> INSTANCE__gallery_walkway_4m__010
- room_number_plaque_candidate.glb -> INSTANCE__room_number_plaque__018
- ribbed_glass_wall_sconce_candidate.glb -> REUSE__ribbed_glass_wall_sconce__020

Each candidate GLB has a single identity-transform root at the original local datum. Apply the existing authoritative instance transform exactly once. Blender source is metres, Z-up, front -Y; exported glTF is metres, Y-up, front +Z: (x,y,z) -> (x,z,-y).

## Exact material path requirements

Read MATERIAL_BINDINGS.json for filenames, sizes/repeat and scalar fallback values. Inspect qa/EXPORT_VALIDATION.json for the actual embedded GLB material/image records.

- glTF baseColorTexture: sRGB decode exactly once. Multiply by any glTF baseColorFactor in linear light
- glTF normalTexture: linear RGB OpenGL +Y tangent-space. Apply normalTexture.scale exactly once: plaster 0.35, concrete 0.25, existing door-paint normal 1.0
- glTF metallicRoughnessTexture: linear data, G = perceptual roughness, B = metalness. Apply glTF factors. These packed GLB images are not the raw single-channel roughness files
- Paint/plaster/concrete metalness is 0. Satin metal is 1.0 by glTF default when metallicFactor is omitted, with roughness 0.30. Do not treat absence of an ORM texture as absence of scalar PBR factors
- Raw source-side roughness PNGs are R8 linear. If building an external-map adapter rather than using the embedded GLB, read their R channel, not G from some unrelated image
- Use repeating mipmapped sampling. Candidate UVs already encode the material-specific scale: plaster 1 x 1m, concrete 2.5 x 2.5m, paint 0.6 x 2m. Do not apply those repeats a second time
- Six wall/floor runtime maps are 1024px. The unmodified 2048px JPEG sources are provenance, not additional runtime textures. Door-paint maps retain their existing supplied size
- The GLBs have UVs and normals; tangents are not promised. Use a correct signed derivative basis or generate tangent space from those UVs/normals, including mirrored bases and transformed meshes
- No AO multiplication or displacement is required. Roughness is not sRGB and not glossiness. Alpha remains opaque; the sconce's glass-looking material is deliberately an opaque approximation, with no new light/emissive source
- Verify the imported-kit model path actually binds these channels. Existing materials_v1 support on generated boxes/doors alone does not prove imported motel GLB support

The relative brightening/contrast reduction of wall and floor is authored in linear-light diffuse processing; do not compensate it again with object tints. REFINEMENT_PARAMETERS.json explains the artistic treatment and hashes. These are plausible painted/maintained finishes, not calibrated measured albedo or measured displacement.

## Optional render-only detail hierarchy

- facade_optional_detail_overlay.glb inherits facade __012, including visibility/removal with its parent
- door_optional_detail_overlay.glb inherits moving door __015, including hinge transform, current angle, destroyed/removed state and all camera/shadow passes. Never leave it at the closed pose when the door opens
- plaque_optional_detail_overlay.glb inherits plaque __018. It is required if using the cleaned plaque candidate: the replacement glyphs are in this overlay

No overlay gets a new collider, nav obstacle, bullet/acoustic blocker, damage cell or gameplay ID. These are multi-node GLBs. Honor their local node transforms; their node-local mesh AABBs alone are not their assembled bounds. Do not normalize or bottom-center them.

## Pivot and clearance guarantee scope

The original input files are unchanged. Original native vertex positions are hash-checked and retained. The candidate only omits cosmetic patch/glyph faces, and adds separately owned detail. Export omits unused vertices, so native preservation does not imply identical GLB vertex counts.

The door's original logical slab is unchanged: local source X [0,1.08]m, Z [0,2.19]m, Y [-0.0225,+0.0225]m. The hinge remains the original local origin; placement remains source (-7.74,0,0), authored yaw 100 degrees. Do not normalize by handle/overlay bounds. Screenshots use a temporary identical 0-degree closed pose only for inspection; the editable checkpoint restores 100 degrees.

Analytic casing-envelope test passed: left casing ends at X=-1.787m, outside the original left inner jamb X=-1.7575m; right casing starts at X=-0.613m, outside original right inner jamb X=-0.6425m; head casing starts at Z=2.248m, above the original header opening Z=2.2225m. The original manifest calls the width 1.11m; the generator's exact inner-face calculation is 1.115m. No casing narrows that opening.

The threshold plate top is +0.4mm, a documented visual offset. This is not a gameplay clearance certification. Existing collision must remain authoritative; we have not rerun in-game controller routes or door physics on this candidate. Removing cosmetic surfaces from rendered meshes is not permission to regenerate compiled collision from them.

## Engine acceptance needed

Match the recorded camera/exposure/light setup as closely as the engine supports, then capture original/candidate with identical settings and door angle. Test channel-debug/PBR-off views, normal +V response, roughness highlights, daylight/room lighting, 0/45/100-degree door poses and parent removal. Check texture residency/deduplication and timing on the target GPU. Keep the original collision/damage/door-ID regressions. Re-run routes if any integration step changes authoritative geometry.

The surrounding window, curtain and air conditioner remain stylized source context. Offline renders do not establish Ready or Not-equivalent fidelity, game performance, gameplay integration or engine approval.
