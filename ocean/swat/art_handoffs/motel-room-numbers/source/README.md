# Briar Court room numbers 101–104, image-only candidate v1

Four small, self-contained runtime GLBs use the accepted Room 101 v3 plaque geometry and materials. V4 inherits this plaque unchanged. The 101 file is an exact binary copy of the accepted baseline. 102–104 replace only its painted-number basecolor and roughness/metallic PNG payloads. No facade, door, other pinned asset, catalog, engine code or gameplay state was changed.

## Runtime integration

Use INSTANCE_BINDINGS.json for explicit per-instance binding: 101→018, 102→042, 103→066, 104→090. Source assembly indices are zero-based; historical engine tag indices are 19/43/67/91, from the existing assembly-plus-one convention. Check the current engine's mapping before using those tags. The supplied engine header confirms every source/world position. Bind replacement models to their existing objects rather than creating new objects or remapping ownership globally.

All runtime node names and extras deliberately retain accepted 101 source provenance, including number_variant=101 and source_owner=...018. They are NOT target instance or displayed-number instructions. Target room/ownership is the explicit sidecar binding. The engine must preserve each existing room object's collision, damage, children, destruction/removal and lifecycle behavior. This material-only variant method is compatible with the existing loader, which already loads embedded GLB textures; it introduces no texture arrays, custom shaders or atlas assumptions.

101 remains the baseline. Apply each other GLB only to its matching plaque instance, with its existing transform exactly once. Local GLB +Y is up; +Z faces outward. Source Blender +Z is up, −Y faces outward. Conversion is (x,y,z)source→(x,z,−y)engine. Preserve both mounting-screw child transforms. Remove any old numeral overlay for that plaque only if it remains active; do not duplicate the accepted plaque or screws.

## Material and typography

The accepted DejaVu Sans Condensed Bold TTF and license are bundled. Artwork is generated at 1024×544 with the exact accepted 382-pixel font size, centering, muted sage RGB(73,92,76), cream RGB(231,229,216), and 173/182 roughness levels. Baseline reproduction is asserted pixel-for-pixel. Front face maps to 0.32×0.17 m. Runtime roughmetal preserves the original packing: R=255, G=roughness, B=255 with metallicFactor=0. Unchanged flat +Y OpenGL normal and painted-edge maps remain embedded. All three material definitions, including double-sided state, are unchanged.

## Files and reproducibility

- runtime/: four actual engine candidates; baseline 101 exactly matches accepted SHA-256 ea1f792eaa83f169034f19a0ba1ce92cb48f9b5b7b8e9db24fa3d24e42c5d08c
- build_numbers.py: deterministic lossless GLB image replacement; run with Python and Pillow. Two consecutive builds must hash identically.
- textures/: editable/generated painted-number inputs, including roughmetal channels
- room_numbers_editable.blend: editable, packed actual-GLB reimport studio scene, four numbers side by side; runtime authority remains the supplied GLBs, not a new Blender export
- reimport_preview.py: recreates the editable file and previews with Blender
- source/: accepted baseline, original typography recipe, bindings, engine assembly/header, and unmodified facade/door context used only in placement renders
- previews/: actual GLB side-by-side reimport, full four-room placement, and 101 placement detail
- qa/: structural/deterministic and independent Blender reimport checks

The context facade/door are unchanged accepted v2 files used only for placement inspection. Their presence is not a new replacement request. Preview lights/cameras are not runtime assets. Placement captures are Blender art checks, not engine screenshots or native validation. Engine review remains responsible for live visibility, lifecycle, image orientation and room assignment.

## Provenance and license

Geometry and authored finishes are inherited from the accepted Briar Court Room101 v3 asset/source and its v2 predecessor, identified in SOURCE_PROVENANCE.json. Existing authored geometry/finish provenance is CC0-1.0; embedded font artwork derives from bundled DejaVu, whose license is retained verbatim. No new external downloads, photos, shaders or font substitutions were used. This is a narrow derivative of the accepted plaque, not a new physical-product replica.
