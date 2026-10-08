# Briar Court roadside sign: painted-letter candidate v1

Bounded source-art replacement for asset 14, owner 140, source instance 139. No uploads or engine edits performed.

## Change
The existing roadside sign now has readable painted/printed BRIAR / COURT / MOTEL and VACANCY artwork. The original two poles, foundation, casing, ochre strips, lower reader board and original edge wear remain. Typeface is the already accepted DejaVu Sans Condensed Bold. Colors are median sRGB pixels from the original embedded teal, cream and red textures, not newly saturated palette approximations. New surfaces carry tiny sparse edge scuffs, with no baked lighting, grime over glyphs or invented branding.

Physical disconnected box-pixel lettering is removed: 1,840 vertices / 2,760 triangles. Two opaque planar printed faces add four triangles. Net result: 3,075 → 319 triangles (−2,756). Faces fit inside the old glyph depth and casing extents. Overall local glTF dimensions remain 3.2 × 4.1125 × 0.70 m. Exact source and exported geometry checks establish unchanged poles/base/casing. No new collider, floorplan or geometry enlargement.

## Baseline and portable source
- baseline/roadside_sign_source_export.glb: an isolated unmodified source rebuild that exactly reproduces the original catalog binary SHA256 588f6b571e3874b37597692c3597de05fcf9c364a64e32fc22d9e0480bd22170 (559,856 bytes). It is not a newly downloaded original.
- baseline/roadside_sign_baseline.blend: isolated original object from pinned motor_court_motel.blend; original packed material images retained.
- source/roadside_sign_editable.blend: editable candidate with packed runtime images.
- runtime/roadside_sign.glb: self-contained native glTF 2.0, OPAQUE materials and embedded images; TEXCOORD_0. No required extensions, AO image or UV1 assertion.
- fonts/: copied existing accepted project font and license.

## Rebuild
Requires Blender 4.3.2 and Python with Pillow. No network needed.
1. python scripts/textures.py
2. blender -b -t 2 --python scripts/build.py
3. python scripts/validate.py
4. blender -b -t 4 --python scripts/preview.py

scripts/inspect_source.py and inspect_mesh.py document original extraction, but require the separately preserved pinned original source path. Rebuild does not require those scripts or the full motel source.

## Binding and proofs
Apply INSTANCE_BINDING.json exactly: world (9.7, −0.08, 8.0), Y-up yaw −0.13 radians, unit scale. Replace rendering resource only and retain engine collision. All previews are fresh GLB reimports. The front and three-quarter previews prove native material/letter import; the 12 m parking view is representative, not matched to the actual engine camera. The supplied actual court image had no camera metadata. Runtime visibility, collision retention and final art acceptance remain engine/user checks.
