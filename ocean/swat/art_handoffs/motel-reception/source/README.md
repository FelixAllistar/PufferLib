# BriarCourt reception canopy sign v1

Compact additive 1.25 × 0.22 × 0.02 m painted panel, with two short tabs bringing the complete envelope to 0.30 m tall. Opaque, static, readable RECEPTION in the same DejaVu Sans Condensed Bold family as accepted room numbers. Original MC15 teal and cream linear palette; modest edge wear only. Existing OFFICE remains untouched.

## Import
Use `runtime/reception_canopy_sign_v1.glb` only. Y-up metres, identity root at the top-mount contact midpoint. World placement: (-10.6, 2.4, 2.01), identity rotation, unit scale. Parking-facing normal is +Z. Complete world bounds: (-11.225, 2.10, 2.0) to (-9.975, 2.40, 2.02). Panel bottom is exactly 2.10 m. Apply transform once. Full binding and owner-local alternative are in INSTANCE_BINDING.json. Owner 5/source 004 is support metadata, not a collider, navmesh or interaction declaration.

Only the new sign is runtime content. Source context is preview-only. This candidate does not edit or replace accepted OFFICE, facade or canopy. Engine verification of walking envelope, clearance and parking readability remains required.

## Channels
Embedded 1024 × 256 PNG basecolor, sRGB, UV0. Base colors are original MC15 linear teal (0.10,0.26,0.25) and cream (0.77,0.75,0.63), converted to sRGB for the image. Opaque PBR; roughness 0.81, metallic 0.12. No normal map needed for flat paint. No baked AO or lighting; AO intentionally unbound/neutral. UV1 is a padded, unique 6 × 3 face atlas reserved for downstream lightmap/AO if needed. No alpha, emission, transmission, animation or external image references.

## Rebuild
Python 3 + Pillow: `python build_sign.py`. This deterministically rebuilds the runtime GLB and texture and compares two builds byte-for-byte. Font and license are bundled. Blender 4.3+: `blender -b --python preview_reimport.py` reimports the exported GLB, checks geometry bounds, saves a packed editable .blend, and renders neutral and original-office placement previews. Context fallback is bundled in source/accepted_office_context.blend. Preview files are offline art checks, not native engine screenshots.

Original source assembly and palette are preserved in the compact context .blend and assembly manifest. Source came from the pinned motor_court_motel.blend used in accepted BriarCourt work; r008 retains this facade/canopy. Typography follows motel_room_numbers_v1 accepted recipe and bundled font. This sign adds no brand marks or extra decoration.

## Licensing
New geometry/artwork/scripts: CC0-1.0, matching original environment dedication. Font: bundled DejaVu license applies separately. No external downloads or uploads were used.
